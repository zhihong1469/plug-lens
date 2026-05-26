/* SPDX-License-Identifier: MIT */
#include "rtsp_server.h"
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
// 宏控开关
// ==========================================================================
#define ENABLE_RTSP_H264      1
#define ENABLE_RTSP_JPEG      0

// ==========================================================================
// RTSP 配置
// ==========================================================================
#define RTSP_SERVER_PORT        8554
#define RTP_MAX_BUFFER_SIZE     (2 * 1024 * 1024)
#define EST_BITRATE_H264_KBPS   500
#define H264_PAYLOAD_TYPE       96
#define MAX_FRAME_SIZE          (2 * 1024 * 1024)

// ==========================================================================
// 前置声明
// ==========================================================================
#if ENABLE_RTSP_H264
class H264MemorySource;
#endif

// 外部声明：客户端检测接口（内部使用）
extern "C" bool rtsp_has_clients(void);

// ==========================================================================
// 全局变量（兼容GCC7.5.0，无atomic编译错误）
// ==========================================================================
#if ENABLE_RTSP_H264
static volatile bool g_frame_wake = false;
static H264MemorySource* g_h264_source = nullptr;

static uint8_t         g_sps_pps_buf[MAX_FRAME_SIZE] = {0};
static uint32_t        g_sps_pps_len = 0;
static uint8_t         g_h264_buf[MAX_FRAME_SIZE] = {0};
static uint32_t        g_h264_size = 0;
static pthread_mutex_t g_h264_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#if ENABLE_RTSP_JPEG
static uint8_t         g_jpeg_buf[MAX_FRAME_SIZE] = {0};
static uint32_t        g_jpeg_size = 0;
static pthread_mutex_t g_jpeg_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// RTSP核心全局对象
static TaskScheduler*      scheduler = nullptr;
static UsageEnvironment*   env = nullptr;
static RTSPServer*         rtspServer = nullptr;
static ServerMediaSession* g_rtsp_sms = nullptr;  // 用于检测客户端
static pthread_t           rtsp_thread;
static volatile bool       rtsp_running = false;
static EventLoopWatchVariable stop_watch;

// ==========================================================================
// H264 内存数据源（主动唤醒机制 + 极致低功耗：无客户端不轮询）
// ==========================================================================
#if ENABLE_RTSP_H264
class H264MemorySource : public FramedSource {
public:
    static H264MemorySource* createNew(UsageEnvironment& env) {
        H264MemorySource* source = new H264MemorySource(env);
        g_h264_source = source;
        return source;
    }

    static void deliverFrame(void* clientData) {
        H264MemorySource* source = (H264MemorySource*)clientData;
        source->doGetNextFrame();
    }

private:
    H264MemorySource(UsageEnvironment& env)
        : FramedSource(env), fNeedSendSPSPPS(True) {}
    virtual ~H264MemorySource() {
        g_h264_source = nullptr;
    }

    virtual void doGetNextFrame() override {
        // 低功耗核心：无客户端直接返回，不执行任何逻辑
        if (!rtsp_has_clients()) {
            return;
        }

        pthread_mutex_lock(&g_h264_mutex);

        // 首次下发SPS/PPS
        if (fNeedSendSPSPPS && g_sps_pps_len > 0) {
            memcpy(fTo, g_sps_pps_buf, g_sps_pps_len);
            fFrameSize = g_sps_pps_len;
            fNeedSendSPSPPS = False;
            pthread_mutex_unlock(&g_h264_mutex);
            afterGetting(this);
            return;
        }

        // 无新帧：仅客户端在线时轮询
        if (g_h264_size == 0 || !g_frame_wake) {
            pthread_mutex_unlock(&g_h264_mutex);
            envir().taskScheduler().scheduleDelayedTask(1000, deliverFrame, this);
            return;
        }

        // 发送最新H264帧
        unsigned frameSize = (fMaxSize < g_h264_size) ? fMaxSize : g_h264_size;
        memcpy(fTo, g_h264_buf, frameSize);
        fFrameSize = frameSize;

        // 清空标志
        g_h264_size = 0;
        g_frame_wake = false;

        pthread_mutex_unlock(&g_h264_mutex);
        afterGetting(this);
    }

    Boolean fNeedSendSPSPPS;
};

// H264 媒体会话
class H264ServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static H264ServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource) {
        return new H264ServerMediaSubsession(env, reuseFirstSource);
    }

protected:
    H264ServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource)
        : OnDemandServerMediaSubsession(env, reuseFirstSource) {}

    virtual FramedSource* createNewStreamSource(unsigned, unsigned& estBitrate) override {
        estBitrate = EST_BITRATE_H264_KBPS;
        H264MemorySource* source = H264MemorySource::createNew(envir());
        return H264VideoStreamFramer::createNew(envir(), source);
    }

    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                                      unsigned char rtpPayloadType,
                                      FramedSource*) override {
        return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadType);
    }
};
#endif

// ==========================================================================
// JPEG 模块（保持原有逻辑）
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
    virtual u_int8_t qFactor()     { return 80; }
    virtual u_int8_t width()       { return GLOBAL_VIDEO_WIDTH / 8; }
    virtual u_int8_t height()      { return GLOBAL_VIDEO_HEIGHT / 8; }

    virtual void doGetNextFrame() override {
        if (!rtsp_has_clients()) {
            return;
        }

        pthread_mutex_lock(&g_jpeg_mutex);
        if (g_jpeg_size == 0) {
            pthread_mutex_unlock(&g_jpeg_mutex);
            envir().taskScheduler().scheduleDelayedTask(40000, fetchFrame, this);
            return;
        }
        fFrameSize = (fMaxSize < g_jpeg_size) ? fMaxSize : g_jpeg_size;
        memcpy(fTo, g_jpeg_buf, fFrameSize);
        fNumTruncatedBytes = 0;
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

    virtual FramedSource* createNewStreamSource(unsigned, unsigned& estBitrate) override {
        estBitrate = 1000;
        return LiveJPEGSource::createNew(envir());
    }

    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, unsigned char, FramedSource*) override {
        return JPEGVideoRTPSink::createNew(envir(), rtpGroupsock);
    }
};
#endif

// ==========================================================================
// RTSP 服务线程
// ==========================================================================
static void* rtsp_server_thread(void* arg) {
    (void)arg;
    rtsp_running = true;
    stop_watch = 0;

    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    // 创建RTSP服务器
    rtspServer = RTSPServer::createNew(*env, RTSP_SERVER_PORT, nullptr);
    if (!rtspServer) {
        *env << "RTSP server create failed!\n";
        rtsp_running = false;
        return nullptr;
    }

    // 创建媒体会话并保存全局指针（用于客户端检测）
    g_rtsp_sms = ServerMediaSession::createNew(*env,
        "stream", "stream", "IMX6ULL RTSP H264 Stream");

#if ENABLE_RTSP_H264
    g_rtsp_sms->addSubsession(H264ServerMediaSubsession::createNew(*env, True));
#endif
#if ENABLE_RTSP_JPEG
    g_rtsp_sms->addSubsession(JPEGServerMediaSubsession::createNew(*env, True));
#endif

    rtspServer->addServerMediaSession(g_rtsp_sms);
    char* url = rtspServer->rtspURL(g_rtsp_sms);
    *env << "=====================================\n";
    *env << "RTSP 服务启动成功\n";
    *env << "播放地址: " << url << "\n";
    *env << "=====================================\n";
    delete[] url;

    // 设置RTP缓冲区大小
    OutPacketBuffer::maxSize = RTP_MAX_BUFFER_SIZE;

    // 启动事件循环
    env->taskScheduler().doEventLoop(&stop_watch);

    // 资源回收
    rtsp_running = false;
    return nullptr;
}

// ==========================================================================
// 对外 C 接口
// ==========================================================================
extern "C" void rtsp_set_sps_pps(const uint8_t* sps_pps, uint32_t len) {
#if ENABLE_RTSP_H264
    if (!sps_pps || len == 0 || len >= MAX_FRAME_SIZE)
        return;
    pthread_mutex_lock(&g_h264_mutex);
    memcpy(g_sps_pps_buf, sps_pps, len);
    g_sps_pps_len = len;
    pthread_mutex_unlock(&g_h264_mutex);
#endif
}

extern "C" int rtsp_start_service(void) {
    if (rtsp_running) return 0;
    return pthread_create(&rtsp_thread, nullptr, rtsp_server_thread, nullptr);
}

extern "C" bool rtsp_is_running(void) {
    return rtsp_running;
}

// 核心推流接口
extern "C" void rtsp_server_push(const uint8_t* buf, uint32_t size) {
    if (!buf || size == 0 || size >= MAX_FRAME_SIZE) return;

#if ENABLE_RTSP_H264
    pthread_mutex_lock(&g_h264_mutex);
    // 实时流：覆盖旧帧，保证最新帧发送
    memcpy(g_h264_buf, buf, size);
    g_h264_size = size;
    g_frame_wake = true;
    pthread_mutex_unlock(&g_h264_mutex);
#elif ENABLE_RTSP_JPEG
    pthread_mutex_lock(&g_jpeg_mutex);
    memcpy(g_jpeg_buf, buf, size);
    g_jpeg_size = size;
    pthread_mutex_unlock(&g_jpeg_mutex);
#endif
}

// ==========================================================================
// ✅【适配你的老版本Live555】官方标准：检测RTSP客户端
// 接口：referenceCount() → 你头文件中明确存在的公有方法
// 含义：引用计数 = 活跃客户端连接数
// ==========================================================================
extern "C" bool rtsp_has_clients(void) {
    // 服务未启动 / 流未创建 → 无客户端
    if (!rtsp_running || !rtspServer || !g_rtsp_sms) {
        return false;
    }

    // 🔥 适配你版本的核心API：referenceCount()
    unsigned clientCount = g_rtsp_sms->referenceCount();
    return (clientCount > 0);
}

// 停止RTSP服务
extern "C" int rtsp_server_stop(void) {
    if (!rtsp_running) return 0;

    // 退出事件循环
    stop_watch = 1;
    pthread_join(rtsp_thread, nullptr);

#if ENABLE_RTSP_H264
    pthread_mutex_lock(&g_h264_mutex);
    g_h264_size = 0;
    g_sps_pps_len = 0;
    g_frame_wake = false;
    pthread_mutex_unlock(&g_h264_mutex);
#endif

#if ENABLE_RTSP_JPEG
    pthread_mutex_lock(&g_jpeg_mutex);
    g_jpeg_size = 0;
    pthread_mutex_unlock(&g_jpeg_mutex);
#endif

    // 释放Live555资源
    if (rtspServer)  Medium::close(rtspServer);
    if (env)         env->reclaim();
    if (scheduler)   delete scheduler;

    // 清空全局指针
    rtspServer = nullptr;
    env = nullptr;
    scheduler = nullptr;
    g_rtsp_sms = nullptr;
    rtsp_running = false;

    return 0;
}

extern "C" int rtsp_server_start(void) {
    return 0;
}