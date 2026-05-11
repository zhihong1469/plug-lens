#include "../inc/net_push_srv.h"
#include "log.h"
#include "queue.h"
#include "frame_link.h"   // 新增：获取FRAME_FMT_YUYV
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#define NET_FRAME_MAGIC     0x12345678
#define UDP_MTU_SIZE       1400

// 自定义推流帧头
typedef struct {
    uint32_t magic;
    uint32_t frame_size;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint64_t timestamp;
} net_frame_header_t;

// 推流模块上下文
typedef struct {
    net_push_srv_config_t config;
    bool is_running;
    bool is_created;
    int sock_fd;
    struct sockaddr_in client_addr;
    bool client_connected;
    Queue_t send_queue;
    void** queue_buffer;
    data_bus_subscription_t data_sub;
    int event_sub_id;
    pthread_t send_thread;
} net_push_srv_ctx_t;

// 函数声明
static void* _send_thread(void* arg);
static void _data_bus_cb(data_bus_item_handle_t item, void* user_data);
static void _event_cb(const event_t* event, void* user_data);
static int _udp_send_frame(net_push_srv_ctx_t* ctx, const uint8_t* data, uint32_t w, uint32_t h, uint32_t fmt);

// ====================== 创建推流服务 ======================
int net_push_srv_create(const net_push_srv_config_t* config, net_push_srv_handle_t* out_handle) {
    if (!config || !out_handle || !config->data_bus || !config->event_bus) return -1;

    net_push_srv_ctx_t* ctx = calloc(1, sizeof(net_push_srv_ctx_t));
    if (!ctx) return -1;

    memcpy(&ctx->config, config, sizeof(net_push_srv_config_t));
    ctx->queue_buffer = malloc(sizeof(void*) * config->queue_capacity);
    Queue_Init(&ctx->send_queue, ctx->queue_buffer, config->queue_capacity);
    ctx->is_created = true;
    *out_handle = ctx;

    LOG_I("NetPush: 创建成功，端口:%d", config->bind_port);
    return 0;
}

// ====================== 启动推流服务 ======================
int net_push_srv_start(net_push_srv_handle_t handle) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)handle;
    if (!ctx || !ctx->is_created || ctx->is_running) return -1;

    // 创建UDP Socket
    ctx->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ctx->config.bind_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(ctx->sock_fd, (struct sockaddr*)&addr, sizeof(addr));

    // ====================== 修复1：DataBus订阅（正确）======================
    data_bus_subscribe(ctx->config.data_bus, DATA_TYPE_VIDEO_FRAME,
                      _data_bus_cb, ctx, &ctx->data_sub);

    // ====================== 修复2：EventBus订阅（严格按接口调用）======================
    event_subscriber_t sub = {
        .event_type = EVENT_TYPE_SYS_STOP,
        .callback = _event_cb,
        .user_data = ctx
    };
    ctx->event_sub_id = event_bus_subscribe(ctx->config.event_bus, &sub);

    // 启动发送线程
    ctx->is_running = true;
    pthread_create(&ctx->send_thread, NULL, _send_thread, ctx);

    LOG_I("NetPush: 服务启动，等待客户端连接");
    return 0;
}

// ====================== 销毁推流服务 ======================
int net_push_srv_destroy(net_push_srv_handle_t handle) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)handle;
    if (!ctx) return 0;

    // 停止线程
    ctx->is_running = false;
    pthread_join(ctx->send_thread, NULL);

    // 取消订阅
    event_bus_unsubscribe(ctx->config.event_bus, ctx->event_sub_id);
    data_bus_unsubscribe(ctx->config.data_bus, &ctx->data_sub);
    close(ctx->sock_fd);

    // 释放队列剩余数据
    void* item = NULL;
    while (Queue_Get(&ctx->send_queue, &item) == 0) {
        data_bus_release(item);
    }

    free(ctx->queue_buffer);
    free(ctx);
    LOG_I("NetPush: 销毁完成");
    return 0;
}

// ====================== DataBus回调：仅指针入队（零拷贝）======================
static void _data_bus_cb(data_bus_item_handle_t item, void* user_data) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)user_data;
    if (!ctx || !ctx->is_running) return;

    // 队列满 → 丢弃旧帧
    while (Queue_IsFull(&ctx->send_queue)) {
        void* old = NULL;
        Queue_Get(&ctx->send_queue, &old);
        data_bus_release(old);
    }

    // 仅存入指针，不拷贝数据
    Queue_Put(&ctx->send_queue, item);
}

// ====================== EventBus停止回调 ======================
static void _event_cb(const event_t* event, void* user_data) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)user_data;
    ctx->is_running = false;
}

// ====================== UDP发送线程（解耦核心）======================
static void* _send_thread(void* arg) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)arg;

    while (ctx->is_running) {
        // ====================== 修复3：客户端握手检测（正确判断非阻塞错误）======================
        if (!ctx->client_connected) {
            char buf[8];
            struct sockaddr_in client;
            socklen_t len = sizeof(client);

            ssize_t ret = recvfrom(ctx->sock_fd, buf, sizeof(buf),
                                  MSG_DONTWAIT, (struct sockaddr*)&client, &len);

            // 只有成功收到客户端握手包，才记录地址
            if (ret >= 0) {
                memcpy(&ctx->client_addr, &client, sizeof(client));
                ctx->client_connected = true;
                LOG_I("NetPush: 客户端连接成功: %s:%d",
                     inet_ntoa(client.sin_addr), ntohs(client.sin_port));
            }
            usleep(10000);
            continue;
        }

        // 从队列取数据指针
        data_bus_item_handle_t item = NULL;
        if (Queue_Get(&ctx->send_queue, &item) != 0) {
            usleep(1000);
            continue;
        }

        // 读取数据（只读，零拷贝）
        const void* data = data_bus_get_readonly_ptr(item);

        // ====================== 修复4：正确视频格式 ======================
        _udp_send_frame(ctx, data, 640, 360, FRAME_FMT_YUYV);

        // 释放总线引用（唯一一次，无野指针）
        data_bus_release(item);
    }
    return NULL;
}

// ====================== UDP分包发送 ======================
static int _udp_send_frame(net_push_srv_ctx_t* ctx, const uint8_t* data, uint32_t w, uint32_t h, uint32_t fmt) {
    net_frame_header_t hdr = {
        .magic = NET_FRAME_MAGIC,
        .frame_size = w * h * 2,
        .width = w,
        .height = h,
        .format = fmt,
        .timestamp = 0
    };

    // 发送帧头
    sendto(ctx->sock_fd, &hdr, sizeof(hdr), 0,
          (struct sockaddr*)&ctx->client_addr, sizeof(ctx->client_addr));

    // 分包发送（MTU 1400）
    size_t total = hdr.frame_size;
    size_t offset = 0;
    while (offset < total) {
        size_t send_len = (total - offset) > UDP_MTU_SIZE ? UDP_MTU_SIZE : (total - offset);
        sendto(ctx->sock_fd, data + offset, send_len, 0,
              (struct sockaddr*)&ctx->client_addr, sizeof(ctx->client_addr));
        offset += send_len;
    }
    return 0;
}