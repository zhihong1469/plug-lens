// 仅使用你提供的官方头文件，无任何额外依赖
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// ========================= 全局JPEG缓存（线程安全） =========================
static uint8_t* g_jpeg_buf = NULL;
static uint32_t g_jpeg_size = 0;
static pthread_mutex_t g_jpeg_mutex = PTHREAD_MUTEX_INITIALIZER;

// ========================= 【核心修复】继承官方JPEGVideoSource（而非FramedSource） =========================
class LiveJPEGSource : public JPEGVideoSource {
public:
    static LiveJPEGSource* createNew(UsageEnvironment& env) {
        return new LiveJPEGSource(env);
    }

    // 静态延时回调（官方调度器要求）
    static void fetchFrame(void* clientData) {
        LiveJPEGSource* source = (LiveJPEGSource*)clientData;
        source->doGetNextFrame();
    }

protected:
    LiveJPEGSource(UsageEnvironment& env) : JPEGVideoSource(env) {}
    virtual ~LiveJPEGSource() {}

    // -------------------------- 必须实现JPEGVideoSource纯虚函数 --------------------------
    virtual u_int8_t type()        { return 0; }   // RFC2435标准JPEG类型
    virtual u_int8_t qFactor()     { return 75; } // 标准JPEG质量因子
    virtual u_int8_t width()       { return 80; } // 640/8=80 (你的分辨率640x360)
    virtual u_int8_t height()      { return 45; } // 360/8=45

    // -------------------------- 核心取数据函数（官方标准） --------------------------
    virtual void doGetNextFrame() {
        pthread_mutex_lock(&g_jpeg_mutex);

        // 无数据则延时重试（适配你的5FPS低帧率）
        if (g_jpeg_buf == NULL || g_jpeg_size == 0) {
            pthread_mutex_unlock(&g_jpeg_mutex);
            envir().taskScheduler().scheduleDelayedTask(10000, fetchFrame, this);
            return;
        }

        // 复制JPEG数据到官方缓冲区
        fFrameSize = (fMaxSize < g_jpeg_size) ? fMaxSize : g_jpeg_size;
        memcpy(fTo, g_jpeg_buf, fFrameSize);
        fNumTruncatedBytes = g_jpeg_size - fFrameSize;

        pthread_mutex_unlock(&g_jpeg_mutex);

        // 通知live555数据就绪（官方强制调用）
        afterGetting(this);
    }
};

// ========================= 官方标准OnDemand子会话（对齐你的demo） =========================
class JPEGServerMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static JPEGServerMediaSubsession* createNew(UsageEnvironment& env, Boolean reuseFirstSource) {
        return new JPEGServerMediaSubsession(env, reuseFirstSource);
    }

protected:
    JPEGServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource)
        : OnDemandServerMediaSubsession(env, reuseFirstSource) {}

    // 【官方标准】创建数据源（直接返回LiveJPEGSource，无Framer）
    virtual FramedSource* createNewStreamSource(unsigned /*clientId*/, unsigned& estBitrate) {
        estBitrate = 1000; // 官方要求：带宽1Mbps，不可为0
        return LiveJPEGSource::createNew(envir());
    }

    // 【官方标准】创建JPEG RTP发送器
    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, 
                                      unsigned char /*payloadType*/, 
                                      FramedSource* /*source*/) {
        return JPEGVideoRTPSink::createNew(envir(), rtpGroupsock);
    }
};

// ========================= RTSP服务全局变量（对齐官方demo） =========================
static TaskScheduler* scheduler = NULL;
static UsageEnvironment* env = NULL;
static RTSPServer* rtspServer = NULL;
static pthread_t rtsp_thread;
static volatile bool rtsp_running = false;
static EventLoopWatchVariable stop_watch;

// ========================= RTSP服务线程（完全照搬你的官方demo逻辑） =========================
static void* rtsp_server_thread(void* arg) {
    (void)arg;
    rtsp_running = true;
    stop_watch = 0;

    // 1. 初始化环境（官方标准）
    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    // 2. 创建RTSP服务器(8554)（官方标准）
    rtspServer = RTSPServer::createNew(*env, 8554, NULL);
    if (rtspServer == NULL) {
        *env << "RTSP server create failed: " << env->getResultMsg() << "\n";
        rtsp_running = false;
        return NULL;
    }

    // 3. 创建媒体会话（官方标准）
    ServerMediaSession* sms = ServerMediaSession::createNew(*env,
        "stream", "stream", "IMX6ULL JPEG RTSP Stream");
    
    // 4. 添加JPEG子会话（多客户端共享流：True）
    sms->addSubsession(JPEGServerMediaSubsession::createNew(*env, True));
    rtspServer->addServerMediaSession(sms);

    // 打印地址
    char* url = rtspServer->rtspURL(sms);
    *env << "=====================================\n";
    *env << "RTSP 服务启动成功\n";
    *env << "播放地址: " << url << "\n";
    *env << "=====================================\n";
    delete[] url;

    // 5. 设置大帧缓冲（官方标准，适配JPEG大帧）
    OutPacketBuffer::maxSize = 2000000;

    // 6. 启动事件循环（官方标准）
    env->taskScheduler().doEventLoop(&stop_watch);

    rtsp_running = false;
    return NULL;
}

// ========================= 对外C接口（完全不变，兼容你的业务代码） =========================
extern "C" int rtsp_server_start(void) {
    if (rtsp_running) return 0;
    return pthread_create(&rtsp_thread, NULL, rtsp_server_thread, NULL);
}

extern "C" void rtsp_server_push_jpeg(const uint8_t* jpeg_buf, uint32_t jpeg_size) {
    if (!jpeg_buf || jpeg_size == 0) return;

    pthread_mutex_lock(&g_jpeg_mutex);
    // 释放旧缓存
    if (g_jpeg_buf) free(g_jpeg_buf);
    // 存储新JPEG帧
    g_jpeg_buf = (uint8_t*)malloc(jpeg_size);
    if (g_jpeg_buf) {
        memcpy(g_jpeg_buf, jpeg_buf, jpeg_size);
        g_jpeg_size = jpeg_size;
    }
    pthread_mutex_unlock(&g_jpeg_mutex);
}

extern "C" int rtsp_server_stop(void) {
    if (!rtsp_running) return 0;

    // 停止循环
    stop_watch = 1;
    pthread_join(rtsp_thread, NULL);

    // 释放资源
    pthread_mutex_lock(&g_jpeg_mutex);
    if (g_jpeg_buf) free(g_jpeg_buf);
    g_jpeg_buf = NULL;
    g_jpeg_size = 0;
    pthread_mutex_unlock(&g_jpeg_mutex);

    if (rtspServer) Medium::close(rtspServer);
    if (env) env->reclaim();
    if (scheduler) delete scheduler;

    rtspServer = NULL;
    env = NULL;
    scheduler = NULL;
    return 0;
}