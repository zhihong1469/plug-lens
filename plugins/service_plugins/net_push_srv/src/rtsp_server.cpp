/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           rtsp_server.cpp
 * @brief          Live555 RTSP 推流服务（H264/JPEG 宏控快速切换）
 * @details        1. 基于Live555官方标准实现，适配IMX6ULL嵌入式平台
 *                 2. 宏定义一键切换 H264 / JPEG 推流格式
 *                 3. On-Demand按需拉流，多客户端共享一路流
 *                 4. 线程安全，全局参数统一，无冗余配置
 * @author         Luo
 * @date           2026
 ******************************************************************************
 */

#include "rtsp_server.h"    // 新增：对接统一头文件
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "vision_ai_config.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// ==========================================================================
// 【宏控开关】核心：一键切换推流格式（二选一，避免冲突）
// ==========================================================================
#define ENABLE_RTSP_H264      1   // 1=启用H264推流  0=关闭
#define ENABLE_RTSP_JPEG      0   // 1=启用JPEG推流  0=关闭（旧版可用逻辑）

// ==========================================================================
// 【全局统一视频基准宏】外部引入，全模块共用，禁止单独修改
// ==========================================================================
#define RTSP_SERVER_PORT        8554        // RTSP服务监听端口
#define RTSP_RETRY_DELAY_US     10000       // 无数据时重试延时(10ms)
#define RTP_MAX_BUFFER_SIZE     2000000     // RTP最大缓冲大小(适配大帧)
#define EST_BITRATE_KBPS        1000        // JPEG预估码率
#define EST_BITRATE_H264_KBPS   500         // H264预估码率
#define H264_PAYLOAD_TYPE       96          // H264 RTP负载类型

// ==========================================================================
// 全局缓存 & 线程锁（按宏控启用，互不干扰）
// ==========================================================================
#if ENABLE_RTSP_H264
static uint8_t*        g_h264_buf = NULL;
static uint32_t        g_h264_size = 0;
static pthread_mutex_t g_h264_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#if ENABLE_RTSP_JPEG
static uint8_t*        g_jpeg_buf = NULL;
static uint32_t        g_jpeg_size = 0;
static pthread_mutex_t g_jpeg_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// ==========================================================================
// H264 视频数据源类（修复版，适配Live554 H264流）
// ==========================================================================
#if ENABLE_RTSP_H264
class LiveH264Source : public FramedSource {
public:
    static LiveH264Source* createNew(UsageEnvironment& env) {
        return new LiveH264Source(env);
    }
    static void fetchFrame(void* clientData) {
        LiveH264Source* source = (LiveH264Source*)clientData;
        source->doGetNextFrame();
    }

protected:
    LiveH264Source(UsageEnvironment& env) : FramedSource(env) {}
    virtual ~LiveH264Source() {}

    virtual void doGetNextFrame() {
        pthread_mutex_lock(&g_h264_mutex);

        // 无数据：延时重试
        if (g_h264_buf == NULL || g_h264_size == 0) {
            pthread_mutex_unlock(&g_h264_mutex);
            envir().taskScheduler().scheduleDelayedTask(RTSP_RETRY_DELAY_US, fetchFrame, this);
            return;
        }

        // 复制H264数据（Annexb格式，带起始码）
        fFrameSize = (fMaxSize < g_h264_size) ? fMaxSize : g_h264_size;
        memcpy(fTo, g_h264_buf, fFrameSize);
        fNumTruncatedBytes = g_h264_size - fFrameSize;

        // 标准时间戳赋值（兼容Live555）
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        fPresentationTime.tv_sec  = ts.tv_sec;
        fPresentationTime.tv_usec = ts.tv_nsec / 1000;

        pthread_mutex_unlock(&g_h264_mutex);
        afterGetting(this);
    }
};

// H264 媒体子会话（官方标准适配）
class H264ServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static H264ServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource) {
        return new H264ServerMediaSubsession(env, reuseFirstSource);
    }

protected:
    H264ServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource)
        : OnDemandServerMediaSubsession(env, reuseFirstSource) {}

    virtual FramedSource* createNewStreamSource(unsigned, unsigned& estBitrate) {
        estBitrate = EST_BITRATE_H264_KBPS;
        FramedSource* source = LiveH264Source::createNew(envir());
        // 关键：适配Annexb格式H264流
        return H264VideoStreamDiscreteFramer::createNew(envir(), source);
    }

    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                                      unsigned char payloadType, FramedSource*) {
        return H264VideoRTPSink::createNew(envir(), rtpGroupsock, payloadType);
    }
};
#endif // ENABLE_RTSP_H264

// ==========================================================================
// JPEG 视频数据源类（你旧版100%可用逻辑，完整保留）
// ==========================================================================
#if ENABLE_RTSP_JPEG
class LiveJPEGSource : public JPEGVideoSource {
public:
    static LiveJPEGSource* createNew(UsageEnvironment& env) {
        return new LiveJPEGSource(env);
    }
    static void fetchFrame(void* clientData) {
        LiveJPEGSource* source = (LiveJPEGSource*)clientData;
        source->doGetNextFrame();
    }

protected:
    LiveJPEGSource(UsageEnvironment& env) : JPEGVideoSource(env) {}
    virtual ~LiveJPEGSource() {}

    virtual u_int8_t type()        { return 0; }
    virtual u_int8_t qFactor()     { return GLOBAL_JPEG_QUALITY; }
    virtual u_int8_t width()       { return GLOBAL_VIDEO_WIDTH / 8; }
    virtual u_int8_t height()      { return GLOBAL_VIDEO_HEIGHT / 8; }

    virtual void doGetNextFrame() {
        pthread_mutex_lock(&g_jpeg_mutex);

        if (g_jpeg_buf == NULL || g_jpeg_size == 0) {
            pthread_mutex_unlock(&g_jpeg_mutex);
            envir().taskScheduler().scheduleDelayedTask(RTSP_RETRY_DELAY_US, fetchFrame, this);
            return;
        }

        fFrameSize = (fMaxSize < g_jpeg_size) ? fMaxSize : g_jpeg_size;
        memcpy(fTo, g_jpeg_buf, fFrameSize);
        fNumTruncatedBytes = g_jpeg_size - fFrameSize;

        pthread_mutex_unlock(&g_jpeg_mutex);
        afterGetting(this);
    }
};

class JPEGServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static JPEGServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource) {
        return new JPEGServerMediaSubsession(env, reuseFirstSource);
    }

protected:
    JPEGServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource)
        : OnDemandServerMediaSubsession(env, reuseFirstSource) {}

    virtual FramedSource* createNewStreamSource(unsigned, unsigned& estBitrate) {
        estBitrate = EST_BITRATE_KBPS;
        return LiveJPEGSource::createNew(envir());
    }

    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                                      unsigned char /*payloadType*/, FramedSource*) {
        return JPEGVideoRTPSink::createNew(envir(), rtpGroupsock);
    }
};
#endif // ENABLE_RTSP_JPEG

// ==========================================================================
// RTSP 服务全局变量
// ==========================================================================
static TaskScheduler*      scheduler = NULL;
static UsageEnvironment*   env = NULL;
static RTSPServer*         rtspServer = NULL;
static pthread_t           rtsp_thread;
static volatile bool       rtsp_running = false;
static EventLoopWatchVariable stop_watch;

// ==========================================================================
// RTSP 服务工作线程（宏控自动加载对应格式）
// ==========================================================================
static void* rtsp_server_thread(void* arg) {
    (void)arg;
    rtsp_running = true;
    stop_watch = 0;

    // 初始化Live555环境
    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    // 创建RTSP服务器
    rtspServer = RTSPServer::createNew(*env, RTSP_SERVER_PORT, NULL);
    if (!rtspServer) {
        *env << "RTSP server create failed!\n";
        rtsp_running = false;
        return NULL;
    }

    // 创建媒体会话
    ServerMediaSession* sms = ServerMediaSession::createNew(*env,
        "stream", "stream", "IMX6ULL RTSP Stream");

    // 宏控添加子会话（自动匹配格式）
#if ENABLE_RTSP_H264
    sms->addSubsession(H264ServerMediaSubsession::createNew(*env, True));
#endif
#if ENABLE_RTSP_JPEG
    sms->addSubsession(JPEGServerMediaSubsession::createNew(*env, True));
#endif

    rtspServer->addServerMediaSession(sms);

    // 打印播放地址
    char* url = rtspServer->rtspURL(sms);
    *env << "=====================================\n";
    *env << "RTSP 服务启动成功\n";
    *env << "播放地址: " << url << "\n";
#if ENABLE_RTSP_H264
    *env << "推流格式: H264\n";
#endif
#if ENABLE_RTSP_JPEG
    *env << "推流格式: JPEG\n";
#endif
    *env << "=====================================\n";
    delete[] url;

    // RTP大帧缓冲
    OutPacketBuffer::maxSize = RTP_MAX_BUFFER_SIZE;

    // 启动事件循环
    env->taskScheduler().doEventLoop(&stop_watch);

    rtsp_running = false;
    return NULL;
}

// ==========================================================================
// 对外 C 语言接口（100% 兼容 rtsp_server.h 声明）
// ==========================================================================
extern "C" int rtsp_server_start(void) {
    if (rtsp_running) return 0;
    return pthread_create(&rtsp_thread, NULL, rtsp_server_thread, NULL);
}

// 统一推流接口（头文件标准接口，宏控内部处理格式）
extern "C" void rtsp_server_push(const uint8_t* buf, uint32_t size) {
    if (!buf || size == 0) return;

#if ENABLE_RTSP_H264
    // H264 格式处理
    pthread_mutex_lock(&g_h264_mutex);
    if (g_h264_buf) free(g_h264_buf);
    g_h264_buf = (uint8_t*)malloc(size);
    if (g_h264_buf) {
        memcpy(g_h264_buf, buf, size);
        g_h264_size = size;
    }
    pthread_mutex_unlock(&g_h264_mutex);
#elif ENABLE_RTSP_JPEG
    // JPEG 格式处理
    pthread_mutex_lock(&g_jpeg_mutex);
    if (g_jpeg_buf) free(g_jpeg_buf);
    g_jpeg_buf = (uint8_t*)malloc(size);
    if (g_jpeg_buf) {
        memcpy(g_jpeg_buf, buf, size);
        g_jpeg_size = size;
    }
    pthread_mutex_unlock(&g_jpeg_mutex);
#endif
}

// 停止服务（自动释放对应格式资源）
extern "C" int rtsp_server_stop(void) {
    if (!rtsp_running) return 0;

    stop_watch = 1;
    pthread_join(rtsp_thread, NULL);

    // 释放H264资源
#if ENABLE_RTSP_H264
    pthread_mutex_lock(&g_h264_mutex);
    if (g_h264_buf) { free(g_h264_buf); g_h264_buf = NULL; g_h264_size = 0; }
    pthread_mutex_unlock(&g_h264_mutex);
#endif

    // 释放JPEG资源
#if ENABLE_RTSP_JPEG
    pthread_mutex_lock(&g_jpeg_mutex);
    if (g_jpeg_buf) { free(g_jpeg_buf); g_jpeg_buf = NULL; g_jpeg_size = 0; }
    pthread_mutex_unlock(&g_jpeg_mutex);
#endif

    // 释放Live555资源
    if (rtspServer) Medium::close(rtspServer);
    if (env) env->reclaim();
    if (scheduler) delete scheduler;

    rtspServer = NULL;
    env = NULL;
    scheduler = NULL;
    return 0;
}