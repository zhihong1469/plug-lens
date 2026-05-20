// ========================= 头文件说明 =========================
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <JPEGVideoRTPSink.hh>
#include <ByteStreamMemoryBufferSource.hh>

#include <pthread.h>
#include <string.h>
#include <unistd.h>

// ========================= 全局变量 =========================
UsageEnvironment* env = nullptr;
TaskScheduler* scheduler = nullptr;
RTSPServer* rtspServer = nullptr;
ServerMediaSession* sms = nullptr;

ByteStreamMemoryBufferSource* g_jpegSource = nullptr;
RTPSink* g_videoSink = nullptr;
RTCPInstance* g_rtcp = nullptr;
Groupsock* rtpGroupsock = nullptr;
Groupsock* rtcpGroupsock = nullptr;

uint8_t g_dummy_jpeg[] = {0xFF, 0xD8, 0xFF, 0xD9};

// 线程控制
static pthread_t g_rtsp_tid = 0;
static volatile bool g_rtsp_running = false;
static pthread_mutex_t g_rtsp_mutex = PTHREAD_MUTEX_INITIALIZER;

// ✅ 修复核心：原子变量 仅声明，不拷贝初始化（避免编译错误）
static EventLoopWatchVariable g_stop_watch_var;

// 前置声明
void play();
void afterPlaying(void* clientData);

// ========================= 播放完成回调 =========================
void afterPlaying(void* clientData) {
    (void)clientData;
    pthread_mutex_lock(&g_rtsp_mutex);

    if (g_videoSink) g_videoSink->stopPlaying();
    if (g_jpegSource) {
        Medium::close(g_jpegSource);
        g_jpegSource = nullptr;
    }

    if (g_rtsp_running) {
        play();
    }

    pthread_mutex_unlock(&g_rtsp_mutex);
}

// ========================= 初始化播放 =========================
void play() {
    g_jpegSource = ByteStreamMemoryBufferSource::createNew(*env,
        g_dummy_jpeg, sizeof(g_dummy_jpeg), False);

    if (g_videoSink && g_jpegSource) {
        g_videoSink->startPlaying(*g_jpegSource, afterPlaying, g_videoSink);
    }
}

// ========================= RTSP 服务主线程 =========================
void* rtsp_server_thread(void* arg) {
    (void)arg;
    g_rtsp_running = true;
    g_stop_watch_var = 0;  // ✅ 修复：运行时赋值，而非初始化时拷贝

    // 1. 创建调度器 + 环境
    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    // 2. 组播地址配置
    struct sockaddr_storage destinationAddress;
    destinationAddress.ss_family = AF_INET;
    ((struct sockaddr_in&)destinationAddress).sin_addr.s_addr = chooseRandomIPv4SSMAddress(*env);

    const unsigned short rtpPortNum = 18888;
    const unsigned short rtcpPortNum = rtpPortNum + 1;
    const unsigned char ttl = 255;

    const Port rtpPort(rtpPortNum);
    const Port rtcpPort(rtcpPortNum);

    // 3. 创建网络套接字
    rtpGroupsock = new Groupsock(*env, destinationAddress, rtpPort, ttl);
    rtpGroupsock->multicastSendOnly();
    rtcpGroupsock = new Groupsock(*env, destinationAddress, rtcpPort, ttl);
    rtcpGroupsock->multicastSendOnly();

    // 4. 缓冲区配置
    OutPacketBuffer::maxSize = 2000000;

    // 5. 创建 JPEG RTP 发送器
    g_videoSink = JPEGVideoRTPSink::createNew(*env, rtpGroupsock);

    // 6. RTCP
    const unsigned estimatedSessionBandwidth = 500;
    const unsigned maxCNAMElen = 100;
    unsigned char CNAME[maxCNAMElen + 1];
    gethostname((char*)CNAME, maxCNAMElen);
    CNAME[maxCNAMElen] = '\0';

    g_rtcp = RTCPInstance::createNew(*env, rtcpGroupsock,
        estimatedSessionBandwidth, CNAME, g_videoSink, nullptr, True);

    // 7. 启动 RTSP 服务器 (端口 8554)
    rtspServer = RTSPServer::createNew(*env, 8554);
    if (!rtspServer) {
        *env << "Failed to create RTSP server\n";
        g_rtsp_running = false;
        return nullptr;
    }

    // 8. 创建媒体会话
    sms = ServerMediaSession::createNew(*env,
        "stream", "mjpeg", "IMX655 MJPEG Stream", True);
    sms->addSubsession(PassiveServerMediaSubsession::createNew(*g_videoSink, g_rtcp));
    rtspServer->addServerMediaSession(sms);

    // 打印播放地址
    char* url = rtspServer->rtspURL(sms);
    *env << "=====================================\n";
    *env << "RTSP URL: " << url << "\n";
    *env << "=====================================\n";
    delete[] url;

    // 9. 启动播放
    play();

    // 标准 live555 事件循环
    env->taskScheduler().doEventLoop(&g_stop_watch_var);

    // 线程退出清理
    g_rtsp_running = false;
    return nullptr;
}

// ========================= C 语言对外接口 =========================
// 启动服务
extern "C" int rtsp_server_start(void) {
    pthread_mutex_lock(&g_rtsp_mutex);
    if (g_rtsp_running || g_rtsp_tid != 0) {
        pthread_mutex_unlock(&g_rtsp_mutex);
        return 0;
    }

    int ret = pthread_create(&g_rtsp_tid, nullptr, rtsp_server_thread, nullptr);
    pthread_mutex_unlock(&g_rtsp_mutex);
    usleep(100000);
    return ret;
}

// 推送 JPEG 帧（线程安全）
extern "C" void rtsp_server_push_jpeg(const uint8_t* jpeg_buf, uint32_t jpeg_size) {
    pthread_mutex_lock(&g_rtsp_mutex);

    if (!g_rtsp_running || !g_videoSink || !jpeg_buf || jpeg_size == 0) {
        pthread_mutex_unlock(&g_rtsp_mutex);
        return;
    }

    g_videoSink->stopPlaying();
    if (g_jpegSource) {
        Medium::close(g_jpegSource);
        g_jpegSource = nullptr;
    }

    g_jpegSource = ByteStreamMemoryBufferSource::createNew(*env,
        (u_int8_t*)jpeg_buf, jpeg_size, False);

    if (g_jpegSource) {
        g_videoSink->startPlaying(*g_jpegSource, afterPlaying, g_videoSink);
    }

    pthread_mutex_unlock(&g_rtsp_mutex);
}

// 安全停止 RTSP 服务
extern "C" int rtsp_server_stop(void) {
    pthread_mutex_lock(&g_rtsp_mutex);

    if (!g_rtsp_running || g_rtsp_tid == 0) {
        pthread_mutex_unlock(&g_rtsp_mutex);
        return 0;
    }

    // 1. 停止运行标志
    g_rtsp_running = false;

    // 2. 停止播放
    if (g_videoSink) {
        g_videoSink->stopPlaying();
    }

    // 触发 live555 事件循环退出
    g_stop_watch_var = 1;

    // 3. 等待线程安全退出
    pthread_mutex_unlock(&g_rtsp_mutex);
    pthread_join(g_rtsp_tid, nullptr);
    pthread_mutex_lock(&g_rtsp_mutex);

    // 4. 释放所有 live555 资源
    if (g_jpegSource)        { Medium::close(g_jpegSource); g_jpegSource = nullptr; }
    if (g_rtcp)              { Medium::close(g_rtcp); g_rtcp = nullptr; }
    if (g_videoSink)         { Medium::close(g_videoSink); g_videoSink = nullptr; }
    if (sms)                 { Medium::close(sms); sms = nullptr; }
    if (rtspServer)          { Medium::close(rtspServer); rtspServer = nullptr; }
    delete rtpGroupsock;      rtpGroupsock = nullptr;
    delete rtcpGroupsock;     rtcpGroupsock = nullptr;
    if (env)                 { env->reclaim(); env = nullptr; }
    if (scheduler)           { delete scheduler; scheduler = nullptr; }

    // 5. 重置变量
    g_rtsp_tid = 0;
    g_stop_watch_var = 0;

    pthread_mutex_unlock(&g_rtsp_mutex);
    return 0;
}