/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           rtsp_server.cpp
 * @brief          Live555 RTSP JPEG推流服务模块
 * @details        1. 基于Live555官方标准实现，适配IMX6ULL嵌入式平台
 *                 2. 支持MJPEG实时流直推，线程安全
 *                 3.  On-Demand按需拉流模式，多客户端共享一路视频流
 *                 4.  对接全局统一视频参数，无冗余配置
 * @author         Luo
 * @date           2026
 ******************************************************************************
 */

// 仅使用官方头文件，无任何额外依赖
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "vision_ai_config.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "codec_api.h"
// ==========================================================================
// 【全局统一视频基准宏】外部引入，全模块共用，禁止单独修改
// ==========================================================================
#define RTSP_VIDEO_FPS                GLOBAL_VIDEO_FPS           // 全局统一帧率（采集=推流=RTSP,仅采集端限流）
#define RTSP_VIDEO_WIDTH              GLOBAL_VIDEO_WIDTH         // 统一分辨率宽640
#define RTSP_VIDEO_HEIGHT             GLOBAL_VIDEO_HEIGHT         // 统一分辨率高360
#define RTSP_JPEG_QUALITY             GLOBAL_JPEG_QUALITY          // 统一JPEG质量

// ==========================================================================
// 【本模块私有宏定义】提取所有魔法数字，便于维护
// ==========================================================================
#define RTSP_SERVER_PORT                8554        // RTSP服务监听端口
#define RTSP_RETRY_DELAY_US             10000       // 无数据时重试延时(10ms)
#define RTP_MAX_BUFFER_SIZE             2000000     // RTP最大缓冲大小(适配大帧JPEG)
#define EST_BITRATE_KBPS                1000        // 预估码率(Kbps，官方要求非0)
#define JPEG_TYPE_STANDARD              0           // RFC2435标准JPEG类型

// ========================= 全局JPEG缓存（线程安全） =========================
/**
 * @brief 全局JPEG帧缓存，用于跨线程传递视频数据
 */
static uint8_t* g_jpeg_buf = NULL;
static uint32_t g_jpeg_size = 0;
static pthread_mutex_t g_jpeg_mutex = PTHREAD_MUTEX_INITIALIZER;  // 线程互斥锁

// ========================= JPEG视频数据源类 =========================
/**
 * @class  LiveJPEGSource
 * @brief  继承Live555官方JPEGVideoSource，实现自定义JPEG数据源
 * @note   必须实现父类纯虚函数，用于提供JPEG格式信息和帧数据
 */
class LiveJPEGSource : public JPEGVideoSource {
public:
    /**
     * @brief  对象创建入口（Live555官方规范）
     * @param  env  Live555运行环境
     * @return 数据源对象指针
     */
    static LiveJPEGSource* createNew(UsageEnvironment& env) {
        return new LiveJPEGSource(env);
    }

    /**
     * @brief  静态延时回调函数，Live555调度器触发获取下一帧
     * @param  clientData  回调参数（当前对象指针）
     */
    static void fetchFrame(void* clientData) {
        LiveJPEGSource* source = (LiveJPEGSource*)clientData;
        source->doGetNextFrame();
    }

protected:
    /**
     * @brief  构造函数（保护权限，Live555官方规范）
     * @param  env  Live555运行环境
     */
    LiveJPEGSource(UsageEnvironment& env) : JPEGVideoSource(env) {}

    /**
     * @brief  析构函数
     */
    virtual ~LiveJPEGSource() {}

    // -------------------------- 必须实现JPEGVideoSource纯虚函数 --------------------------
    /**
     * @brief  获取JPEG类型（RFC2435标准）
     * @return JPEG类型编码
     */
    virtual u_int8_t type()        { return JPEG_TYPE_STANDARD; }

    /**
     * @brief  获取JPEG质量因子
     * @return 质量值
     */
    virtual u_int8_t qFactor()     { return RTSP_JPEG_QUALITY; }

    /**
     * @brief  获取JPEG宽度（Live555要求：实际宽度/8）
     * @return 标准化宽度值
     */
    virtual u_int8_t width()       { return RTSP_VIDEO_WIDTH / 8; }

    /**
     * @brief  获取JPEG高度（Live555要求：实际高度/8）
     * @return 标准化高度值
     */
    virtual u_int8_t height()      { return RTSP_VIDEO_HEIGHT / 8; }

    // -------------------------- 核心取数据函数（官方标准） --------------------------
    /**
     * @brief  Live555核心回调：获取下一帧视频数据
     * @note   从全局缓存读取JPEG数据，拷贝到Live555内部缓冲区
     */
    virtual void doGetNextFrame() {
        pthread_mutex_lock(&g_jpeg_mutex);

        // 无有效数据，延时重试（适配低帧率场景）
        if (g_jpeg_buf == NULL || g_jpeg_size == 0) {
            pthread_mutex_unlock(&g_jpeg_mutex);
            envir().taskScheduler().scheduleDelayedTask(RTSP_RETRY_DELAY_US, fetchFrame, this);
            return;
        }

        // 复制JPEG数据到官方缓冲区，防止超出最大限制
        fFrameSize = (fMaxSize < g_jpeg_size) ? fMaxSize : g_jpeg_size;
        memcpy(fTo, g_jpeg_buf, fFrameSize);
        fNumTruncatedBytes = g_jpeg_size - fFrameSize;

        pthread_mutex_unlock(&g_jpeg_mutex);

        // 官方强制调用：通知数据已就绪
        afterGetting(this);
    }
};

// ========================= RTSP按需媒体子会话类 =========================
/**
 * @class  JPEGServerMediaSubsession
 * @brief  Live555官方标准按需子会话，管理JPEG流的RTP发送
 */
class JPEGServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    /**
     * @brief  对象创建入口
     * @param  env              Live555运行环境
     * @param  reuseFirstSource 是否多客户端复用一路数据源
     * @return 子会话对象指针
     */
    static JPEGServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource) {
        return new JPEGServerMediaSubsession(env, reuseFirstSource);
    }

protected:
    /**
     * @brief  构造函数
     * @param  env              Live555运行环境
     * @param  reuseFirstSource 是否多客户端复用一路数据源
     */
    JPEGServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource)
        : OnDemandServerMediaSubsession(env, reuseFirstSource) {}

    /**
     * @brief  创建视频数据源（官方回调）
     * @param  clientId   客户端ID
     * @param  estBitrate 输出预估码率
     * @return 帧数据源指针
     */
    virtual FramedSource* createNewStreamSource(unsigned /*clientId*/, unsigned& estBitrate) {
        estBitrate = EST_BITRATE_KBPS;
        return LiveJPEGSource::createNew(envir());
    }

    /**
     * @brief  创建RTP发送器（官方回调）
     * @param  rtpGroupsock  RTP网络套接字
     * @param  payloadType   RTP负载类型
     * @param  source        视频数据源
     * @return RTP发送器指针
     */
    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, 
                                      unsigned char /*payloadType*/, 
                                      FramedSource* /*source*/) {
        return JPEGVideoRTPSink::createNew(envir(), rtpGroupsock);
    }
};

// ========================= RTSP服务全局变量 =========================
static TaskScheduler* scheduler = NULL;       // Live555任务调度器
static UsageEnvironment* env = NULL;          // Live555运行环境
static RTSPServer* rtspServer = NULL;         // RTSP服务器实例
static pthread_t rtsp_thread;                 // RTSP服务线程
static volatile bool rtsp_running = false;    // 服务运行标志
static EventLoopWatchVariable stop_watch;     // 事件循环停止标志

// ========================= RTSP服务工作线程 =========================
/**
 * @brief  RTSP服务主线程
 * @param  arg  线程参数
 * @return 线程退出码
 */
static void* rtsp_server_thread(void* arg) {
    (void)arg;
    rtsp_running = true;
    stop_watch = 0;

    // 1. 初始化Live555标准运行环境
    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    // 2. 创建RTSP服务器实例
    rtspServer = RTSPServer::createNew(*env, RTSP_SERVER_PORT, NULL);
    if (rtspServer == NULL) {
        *env << "RTSP server create failed: " << env->getResultMsg() << "\n";
        rtsp_running = false;
        return NULL;
    }

    // 3. 创建媒体会话并绑定流信息
    ServerMediaSession* sms = ServerMediaSession::createNew(*env,
        "stream", "stream", "IMX6ULL JPEG RTSP Stream");
    
    // 4. 添加JPEG子会话，开启多客户端共享
    sms->addSubsession(JPEGServerMediaSubsession::createNew(*env, True));
    rtspServer->addServerMediaSession(sms);

    // 打印RTSP播放地址
    char* url = rtspServer->rtspURL(sms);
    *env << "=====================================\n";
    *env << "RTSP 服务启动成功\n";
    *env << "播放地址: " << url << "\n";
    *env << "=====================================\n";
    delete[] url;

    // 5. 设置RTP大帧缓冲，适配MJPEG大分辨率帧
    OutPacketBuffer::maxSize = RTP_MAX_BUFFER_SIZE;

    // 6. 启动Live555事件循环（阻塞运行，直到stop_watch=1）
    env->taskScheduler().doEventLoop(&stop_watch);

    rtsp_running = false;
    return NULL;
}

// ========================= 对外C语言接口（兼容业务模块调用） =========================
/**
 * @brief  启动RTSP推流服务
 * @return 0:成功/已运行  负数:失败
 */
extern "C" int rtsp_server_start(void) {
    if (rtsp_running) return 0;
    return pthread_create(&rtsp_thread, NULL, rtsp_server_thread, NULL);
}

/**
 * @brief  推送JPEG帧到RTSP服务
 * @param  jpeg_buf  JPEG数据指针
 * @param  jpeg_size JPEG数据长度
 */
extern "C" void rtsp_server_push_jpeg(const uint8_t* jpeg_buf, uint32_t jpeg_size) {
    if (!jpeg_buf || jpeg_size == 0) return;

    pthread_mutex_lock(&g_jpeg_mutex);
    // 释放旧帧缓存，避免内存泄漏
    if (g_jpeg_buf) free(g_jpeg_buf);
    // 分配新缓存并拷贝JPEG数据
    g_jpeg_buf = (uint8_t*)malloc(jpeg_size);
    if (g_jpeg_buf) {
        memcpy(g_jpeg_buf, jpeg_buf, jpeg_size);
        g_jpeg_size = jpeg_size;
    }
    // 调试打印：JPEG帧头/尾校验+大小
    printf("JPEG start: 0x%02X%02X, end: 0x%02X%02X, size: %u\n",
       jpeg_buf[0], jpeg_buf[1], jpeg_buf[jpeg_size-2], jpeg_buf[jpeg_size-1], jpeg_size);
    pthread_mutex_unlock(&g_jpeg_mutex);
}

/**
 * @brief  停止RTSP推流服务并释放所有资源
 * @return 0:成功
 */
extern "C" int rtsp_server_stop(void) {
    if (!rtsp_running) return 0;

    // 停止事件循环，退出线程
    stop_watch = 1;
    pthread_join(rtsp_thread, NULL);

    // 释放JPEG全局缓存
    pthread_mutex_lock(&g_jpeg_mutex);
    if (g_jpeg_buf) free(g_jpeg_buf);
    g_jpeg_buf = NULL;
    g_jpeg_size = 0;
    pthread_mutex_unlock(&g_jpeg_mutex);

    // 释放Live555官方资源
    if (rtspServer) Medium::close(rtspServer);
    if (env) env->reclaim();
    if (scheduler) delete scheduler;

    // 重置全局指针
    rtspServer = NULL;
    env = NULL;
    scheduler = NULL;
    return 0;
}