/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           rtsp_server.cpp
 * @brief          Live555 RTSP H.264推流（JPEG兼容，宏控切换）
 ******************************************************************************
 */
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
// 【宏控开关】核心：单独启用H264，关闭JPEG（解决冲突）
// ==========================================================================
#define ENABLE_RTSP_H264      1   // 启用H264推流
#define ENABLE_RTSP_JPEG      0   // 禁用JPEG推流（避免冲突）

// ==========================================================================
// 全局配置
// ==========================================================================
#define RTSP_SERVER_PORT        8554
#define RTSP_RETRY_DELAY_US     10000
#define RTP_MAX_BUFFER_SIZE     2000000
#define EST_BITRATE_H264_KBPS   500
#define H264_PAYLOAD_TYPE       96

// ========================= H264全局缓存（线程安全） =========================
static uint8_t* g_h264_buf = NULL;
static uint32_t g_h264_size = 0;
static pthread_mutex_t g_h264_mutex = PTHREAD_MUTEX_INITIALIZER;

// ========================= JPEG全局缓存（保留，宏控关闭） =========================
#if ENABLE_RTSP_JPEG
static uint8_t* g_jpeg_buf = NULL;
static uint32_t g_jpeg_size = 0;
static pthread_mutex_t g_jpeg_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// ========================= H.264视频数据源（修复时间戳） =========================
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

        // 无数据重试
        if (g_h264_buf == NULL || g_h264_size == 0) {
            pthread_mutex_unlock(&g_h264_mutex);
            envir().taskScheduler().scheduleDelayedTask(RTSP_RETRY_DELAY_US, fetchFrame, this);
            return;
        }

        // 复制数据
        fFrameSize = (fMaxSize < g_h264_size) ? fMaxSize : g_h264_size;
        memcpy(fTo, g_h264_buf, fFrameSize);
        fNumTruncatedBytes = g_h264_size - fFrameSize;

        // 修复：timeval 与 timespec 兼容赋值
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        fPresentationTime.tv_sec = ts.tv_sec;
        fPresentationTime.tv_usec = ts.tv_nsec / 1000;

        pthread_mutex_unlock(&g_h264_mutex);
        afterGetting(this);
    }
};

// ========================= H264媒体子会话 =========================
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
        return H264VideoStreamDiscreteFramer::createNew(envir(), source);
    }

    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                                      unsigned char payloadType, FramedSource*) {
        return H264VideoRTPSink::createNew(envir(), rtpGroupsock, payloadType);
    }
};

// ========================= JPEG子会话（保留） =========================
#if ENABLE_RTSP_JPEG
class LiveJPEGSource : public JPEGVideoSource {
public:
    static LiveJPEGSource* createNew(UsageEnvironment& env) { return new LiveJPEGSource(env); }
    static void fetchFrame(void* clientData) { ((LiveJPEGSource*)clientData)->doGetNextFrame(); }
protected:
    LiveJPEGSource(UsageEnvironment& env) : JPEGVideoSource(env) {}
    virtual u_int8_t type() { return 0; }
    virtual u_int8_t qFactor() { return GLOBAL_JPEG_QUALITY; }
    virtual u_int8_t width() { return GLOBAL_VIDEO_WIDTH / 8; }
    virtual u_int8_t height() { return GLOBAL_VIDEO_HEIGHT / 8; }
    virtual void doGetNextFrame() {
        pthread_mutex_lock(&g_jpeg_mutex);
        if (!g_jpeg_buf || g_jpeg_size == 0) {
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
    static JPEGServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuse) {
        return new JPEGServerMediaSubsession(env, reuse);
    }
protected:
    JPEGServerMediaSubsession(UsageEnvironment& env, Boolean reuse) : OnDemandServerMediaSubsession(env, reuse) {}
    virtual FramedSource* createNewStreamSource(unsigned, unsigned& est) { est=1000; return LiveJPEGSource::createNew(envir()); }
    virtual RTPSink* createNewRTPSink(Groupsock* g, unsigned char, FramedSource*) {
        return JPEGVideoRTPSink::createNew(envir(), g);
    }
};
#endif

// ========================= RTSP全局变量 =========================
static TaskScheduler* scheduler = NULL;
static UsageEnvironment* env = NULL;
static RTSPServer* rtspServer = NULL;
static pthread_t rtsp_thread;
static volatile bool rtsp_running = false;
static EventLoopWatchVariable stop_watch;

// ========================= RTSP线程 =========================
static void* rtsp_server_thread(void* arg) {
    (void)arg;
    rtsp_running = true;
    stop_watch = 0;

    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    rtspServer = RTSPServer::createNew(*env, RTSP_SERVER_PORT, NULL);
    if (!rtspServer) { *env << "RTSP创建失败\n"; rtsp_running=false; return NULL; }

    // ============== 核心：只创建H264会话，杜绝冲突 ==============
    ServerMediaSession* sms = ServerMediaSession::createNew(*env, "stream", "stream", "IMX6ULL H264 RTSP");
#if ENABLE_RTSP_H264
    sms->addSubsession(H264ServerMediaSubsession::createNew(*env, True));
#endif
#if ENABLE_RTSP_JPEG
    sms->addSubsession(JPEGServerMediaSubsession::createNew(*env, True));
#endif
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "=====================================\n";
    *env << "RTSP 服务启动\n地址: " << url << "\n格式: H264\n";
    *env << "=====================================\n";
    delete[] url;

    OutPacketBuffer::maxSize = RTP_MAX_BUFFER_SIZE;
    env->taskScheduler().doEventLoop(&stop_watch);

    rtsp_running = false;
    return NULL;
}

// ========================= 对外C接口 =========================
extern "C" int rtsp_server_start(void) {
    if (rtsp_running) return 0;
    return pthread_create(&rtsp_thread, NULL, rtsp_server_thread, NULL);
}

#if ENABLE_RTSP_JPEG
extern "C" void rtsp_server_push_jpeg(const uint8_t* jpeg_buf, uint32_t jpeg_size) {
    if (!jpeg_buf || jpeg_size == 0) return;
    pthread_mutex_lock(&g_jpeg_mutex);
    if (g_jpeg_buf) free(g_jpeg_buf);
    g_jpeg_buf = (uint8_t*)malloc(jpeg_size);
    if (g_jpeg_buf) { memcpy(g_jpeg_buf, jpeg_buf, jpeg_size); g_jpeg_size = jpeg_size; }
    pthread_mutex_unlock(&g_jpeg_mutex);
}
#endif

extern "C" void rtsp_server_push_h264(const uint8_t* h264_buf, uint32_t h264_size) {
    if (!h264_buf || h264_size == 0) return;

    pthread_mutex_lock(&g_h264_mutex);
    // 静态缓存优化，避免频繁malloc
    if (g_h264_buf) free(g_h264_buf);
    g_h264_buf = (uint8_t*)malloc(h264_size);
    if (g_h264_buf) {
        memcpy(g_h264_buf, h264_buf, h264_size);
        g_h264_size = h264_size;

        // ============== 调试：打印H264起始码+NALU类型 ==============
        printf("[RTSP-H264] 帧大小：%u | 起始码：0x%02X%02X%02X%02X | NALU类型：%u\n",
               h264_size,
               h264_buf[0], h264_buf[1], h264_buf[2], h264_buf[3],
               h264_buf[4] & 0x1F);
    }
    pthread_mutex_unlock(&g_h264_mutex);
}

extern "C" int rtsp_server_stop(void) {
    if (!rtsp_running) return 0;
    stop_watch = 1;
    pthread_join(rtsp_thread, NULL);

#if ENABLE_RTSP_JPEG
    pthread_mutex_lock(&g_jpeg_mutex);
    if (g_jpeg_buf) free(g_jpeg_buf); g_jpeg_buf=NULL; g_jpeg_size=0;
    pthread_mutex_unlock(&g_jpeg_mutex);
#endif

    pthread_mutex_lock(&g_h264_mutex);
    if (g_h264_buf)
    {
        free(g_h264_buf);
        g_h264_buf=NULL; 
        g_h264_size=0;
    }
    pthread_mutex_unlock(&g_h264_mutex);

    if (rtspServer) Medium::close(rtspServer);
    if (env) env->reclaim();
    if (scheduler) delete scheduler;
    rtspServer=NULL; env=NULL; scheduler=NULL;
    return 0;
}