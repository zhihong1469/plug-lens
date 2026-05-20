#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <JPEGVideoRTPSink.hh>
#include <ByteStreamMemoryBufferSource.hh>

UsageEnvironment* env;
ByteStreamMemoryBufferSource* g_jpegSource = NULL;
RTPSink* g_videoSink = NULL;
uint8_t g_dummy_jpeg[] = {0xFF, 0xD8, 0xFF, 0xD9};

void play();

// RTSP服务线程
void* rtsp_server_thread(void* arg) {
    (void)arg;

    TaskScheduler* scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    struct sockaddr_storage destinationAddress;
    destinationAddress.ss_family = AF_INET;
    ((struct sockaddr_in&)destinationAddress).sin_addr.s_addr = chooseRandomIPv4SSMAddress(*env);

    const unsigned short rtpPortNum = 18888;
    const unsigned short rtcpPortNum = rtpPortNum+1;
    const unsigned char ttl = 255;

    const Port rtpPort(rtpPortNum);
    const Port rtcpPort(rtcpPortNum);

    Groupsock rtpGroupsock(*env, destinationAddress, rtpPort, ttl);
    rtpGroupsock.multicastSendOnly();
    Groupsock rtcpGroupsock(*env, destinationAddress, rtcpPort, ttl);
    rtcpGroupsock.multicastSendOnly();

    OutPacketBuffer::maxSize = 2000000;
    g_videoSink = JPEGVideoRTPSink::createNew(*env, &rtpGroupsock);

    const unsigned estimatedSessionBandwidth = 500;
    const unsigned maxCNAMElen = 100;
    unsigned char CNAME[maxCNAMElen+1];
    gethostname((char*)CNAME, maxCNAMElen);
    CNAME[maxCNAMElen] = '\0';

    RTCPInstance* rtcp = RTCPInstance::createNew(*env, &rtcpGroupsock,
        estimatedSessionBandwidth, CNAME,
        g_videoSink, NULL, True);

    RTSPServer* rtspServer = RTSPServer::createNew(*env, 8554);
    if (rtspServer == NULL) {
        *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
        return NULL;
    }

    ServerMediaSession* sms = ServerMediaSession::createNew(*env,
        "stream", "mjpeg", "IMX655 MJPEG Stream", True);

    sms->addSubsession(PassiveServerMediaSubsession::createNew(*g_videoSink, rtcp));
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "=====================================\n";
    *env << "RTSP URL: " << url << "\n";
    *env << "=====================================\n";
    delete[] url;

    play();
    env->taskScheduler().doEventLoop();

    return NULL;
}

void afterPlaying(void* clientData) {
    (void)clientData;
    g_videoSink->stopPlaying();
    Medium::close(g_jpegSource);
    play();
}

void play() {
    g_jpegSource = ByteStreamMemoryBufferSource::createNew(*env,
        g_dummy_jpeg, sizeof(g_dummy_jpeg), False);

    g_videoSink->startPlaying(*g_jpegSource, afterPlaying, g_videoSink);
}

extern "C" int rtsp_server_start(void) {
    pthread_t tid;
    return pthread_create(&tid, NULL, rtsp_server_thread, NULL);
}

// 🔥 修复：强制转换 const 指针为非const（适配live555老版本API）
extern "C" void rtsp_server_push_jpeg(const uint8_t* jpeg_buf, uint32_t jpeg_size) {
    if (!g_jpegSource || !jpeg_buf || jpeg_size == 0) return;

    g_videoSink->stopPlaying();
    Medium::close(g_jpegSource);

    // ✅ 核心修复：强转 const 指针
    g_jpegSource = ByteStreamMemoryBufferSource::createNew(*env,
        (u_int8_t*)jpeg_buf, jpeg_size, False);

    g_videoSink->startPlaying(*g_jpegSource, afterPlaying, g_videoSink);
}