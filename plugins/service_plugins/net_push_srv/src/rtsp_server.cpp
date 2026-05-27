/* SPDX-License-Identifier: MIT */
#include "rtsp_server.h"
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "vision_ai_config.h"
#include "data_bus.h"  // 🔥 引入数据总线
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

// 🔥 绑定H264数据总线
#define H264_DATA_BUS_NAME        H264_RTSP_DATA_BUS_NAME

// ==========================================================================
// 前置声明
// ==========================================================================
#if ENABLE_RTSP_H264
class H264MemorySource;
#endif

extern "C" bool rtsp_has_clients(void);

// ==========================================================================
// 全局变量（🔥 全部删除静态H264缓存）
// ==========================================================================
#if ENABLE_RTSP_H264
static H264MemorySource* g_h264_source = nullptr;
static uint8_t         g_sps_pps_buf[MAX_FRAME_SIZE] = {0};
static uint32_t        g_sps_pps_len = 0;
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
static ServerMediaSession* g_rtsp_sms = nullptr;
static pthread_t           rtsp_thread;
static volatile bool       rtsp_running = false;
static EventLoopWatchVariable stop_watch;

// ==========================================================================
// 客户端计数
// ==========================================================================
static volatile uint32_t g_rtsp_client_count = 0;
static pthread_mutex_t   g_client_mutex = PTHREAD_MUTEX_INITIALIZER;

// ==========================================================================
// 🔥 革命核心：H264内存数据源（从数据总线拉取）
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
        if (source != nullptr && !source->fIsDestroyed) {
            source->doGetNextFrame();
        }
    }

private:
    H264MemorySource(UsageEnvironment& env)
        : FramedSource(env), fNeedSendSPSPPS(True), fIsDestroyed(False), fScheduleId(0) {}
    virtual ~H264MemorySource() {
        if (fScheduleId != 0) {
            envir().taskScheduler().unscheduleDelayedTask(fScheduleId);
            fScheduleId = 0;
        }
        fIsDestroyed = True;
        g_h264_source = nullptr;
    }

    virtual void doGetNextFrame() override {
        // 无客户端休眠
        if (!rtsp_has_clients()) {
            fScheduleId = envir().taskScheduler().scheduleDelayedTask(10000, deliverFrame, this);
            return;
        }

        // 1. 首次发送SPS/PPS
        if (fNeedSendSPSPPS && g_sps_pps_len > 0) {
            memcpy(fTo, g_sps_pps_buf, g_sps_pps_len);
            fFrameSize = g_sps_pps_len;
            fNeedSendSPSPPS = False;
            afterGetting(this);
            return;
        }

        // 🔥 2. 从数据总线拉取最新H264帧（零拷贝）
        data_bus_item_handle_t h264_item = NULL;
        if (data_bus_pull_latest(H264_DATA_BUS_NAME, DATA_TYPE_H264, &h264_item) != DATA_BUS_OK) {
            fScheduleId = envir().taskScheduler().scheduleDelayedTask(500, deliverFrame, this);
            return;
        }

        // 3. 读取总线数据
        const uint8_t* h264_data = (const uint8_t*)data_bus_get_readonly_ptr(h264_item);
        size_t h264_size = data_bus_get_item_size(h264_item);

        if (h264_data && h264_size > 0) {
            unsigned frameSize = (fMaxSize < h264_size) ? fMaxSize : h264_size;
            memcpy(fTo, h264_data, frameSize);
            fFrameSize = frameSize;
        } else {
            fFrameSize = 0;
        }

        // 4. 释放总线引用
        data_bus_release(h264_item);

        if (fFrameSize == 0) {
            fScheduleId = envir().taskScheduler().scheduleDelayedTask(500, deliverFrame, this);
            return;
        }

        afterGetting(this);
    }

    Boolean fNeedSendSPSPPS;
    Boolean fIsDestroyed;
    TaskToken fScheduleId;
};

// ==========================================================================
// 自定义H264会话
// ==========================================================================
class CustomH264Subsession : public OnDemandServerMediaSubsession {
public:
    static CustomH264Subsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource) {
        return new CustomH264Subsession(env, reuseFirstSource);
    }

protected:
    CustomH264Subsession(UsageEnvironment& env, Boolean reuseFirstSource)
        : OnDemandServerMediaSubsession(env, reuseFirstSource) {}

    virtual void startStream(unsigned clientSessionId, void* streamToken,
                             TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
                             unsigned short& rtpSeqNum, unsigned& rtpTimestamp,
                             ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                             void* serverRequestAlternativeByteHandlerClientData) override {
        pthread_mutex_lock(&g_client_mutex);
        g_rtsp_client_count++;
        pthread_mutex_unlock(&g_client_mutex);
        printf("[RTSP] 客户端连接 | 在线数: %u\n", g_rtsp_client_count);

        OnDemandServerMediaSubsession::startStream(clientSessionId, streamToken,
                    rtcpRRHandler, rtcpRRHandlerClientData,
                    rtpSeqNum, rtpTimestamp,
                    serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);
    }

    virtual void deleteStream(unsigned clientSessionId, void*& streamToken) override {
        pthread_mutex_lock(&g_client_mutex);
        if (g_rtsp_client_count > 0) g_rtsp_client_count--;
        pthread_mutex_unlock(&g_client_mutex);
        printf("[RTSP] 客户端断开 | 在线数: %u\n", g_rtsp_client_count);

        OnDemandServerMediaSubsession::deleteStream(clientSessionId, streamToken);
    }

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
// JPEG 模块
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
            envir().taskScheduler().scheduleDelayedTask(40000, fetchFrame, this);
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
    g_rtsp_client_count = 0;

    // 设置实时优先级
    struct sched_param param;
    param.sched_priority = 85;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    rtspServer = RTSPServer::createNew(*env, RTSP_SERVER_PORT, nullptr);
    if (!rtspServer) {
        *env << "RTSP server create failed!\n";
        rtsp_running = false;
        return nullptr;
    }

    g_rtsp_sms = ServerMediaSession::createNew(*env,
        "stream", "stream", "IMX6ULL RTSP H264 Stream");

#if ENABLE_RTSP_H264
    g_rtsp_sms->addSubsession(CustomH264Subsession::createNew(*env, True));
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

    OutPacketBuffer::maxSize = RTP_MAX_BUFFER_SIZE;
    env->taskScheduler().doEventLoop(&stop_watch);

    rtsp_running = false;
    return nullptr;
}

// ==========================================================================
// 对外 C 接口
// ==========================================================================
extern "C" void rtsp_set_sps_pps(const uint8_t* sps_pps, uint32_t len) {
#if ENABLE_RTSP_H264
    if (!sps_pps || len == 0 || len >= MAX_FRAME_SIZE) return;
    memcpy(g_sps_pps_buf, sps_pps, len);
    g_sps_pps_len = len;
#endif
}

extern "C" int rtsp_start_service(void) {
    if (rtsp_running) return 0;
    return pthread_create(&rtsp_thread, nullptr, rtsp_server_thread, nullptr);
}

extern "C" bool rtsp_is_running(void) {
    return rtsp_running;
}

// 🔥 废弃：删除原推流接口（数据总线替代）
extern "C" void rtsp_server_push(const uint8_t* buf, uint32_t size) {
    // 空实现，兼容旧代码
    (void)buf; (void)size;
}

extern "C" bool rtsp_has_clients(void) {
    if (!rtsp_running) return false;
    bool result = false;
    pthread_mutex_lock(&g_client_mutex);
    result = (g_rtsp_client_count > 0);
    pthread_mutex_unlock(&g_client_mutex);
    return result;
}

extern "C" int rtsp_server_stop(void) {
    if (!rtsp_running) return 0;

    stop_watch = 1;
    pthread_join(rtsp_thread, nullptr);

    pthread_mutex_lock(&g_client_mutex);
    g_rtsp_client_count = 0;
    pthread_mutex_unlock(&g_client_mutex);

#if ENABLE_RTSP_H264
    g_sps_pps_len = 0;
#endif

    if (rtspServer)  Medium::close(rtspServer);
    if (env)         env->reclaim();
    if (scheduler)   delete scheduler;

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