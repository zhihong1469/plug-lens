/* SPDX-License-Identifier: MIT */
#include "../inc/net_push_srv.h"
#include "log.h"
#include "queue.h"
// 帧链接头文件，获取视频格式定义
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

// ==============================================
// 宏定义配置
// ==============================================
// 自定义视频帧头魔数，客户端用于校验帧头合法性
#define NET_FRAME_MAGIC     0x12345678
// UDP最大传输单元，避免分包过大丢包
#define UDP_MTU_SIZE       1400
// 推流核心配置：仅缓存最新1帧，实时视频无需多帧缓存
#define NET_PUSH_MAX_FRAME  1

// ==============================================
// 自定义UDP视频帧头结构体
// 客户端通过该结构体解析视频宽高、格式、数据长度
// ==============================================
typedef struct {
    uint32_t magic;        // 魔数：0x12345678
    uint32_t frame_size;   // 视频帧总数据大小
    uint32_t width;        // 视频宽度
    uint32_t height;       // 视频高度
    uint32_t format;       // 视频格式（YUYV）
    uint64_t timestamp;    // 时间戳（暂未使用）
} net_frame_header_t;

// ==============================================
// 推流服务上下文结构体
// 管理推流所有资源、状态、句柄
// ==============================================
typedef struct {
    net_push_srv_config_t config;     // 外部传入的配置参数
    bool is_running;                  // 服务运行标志
    bool is_created;                  // 服务创建完成标志
    int sock_fd;                      // UDP socket文件描述符
    struct sockaddr_in client_addr;   // 客户端地址信息
    bool client_connected;            // 客户端连接状态
    Queue_t send_queue;               // 发送队列（仅存1帧）
    void** queue_buffer;              // 队列缓冲区（指针数组）
    data_bus_subscription_handle_t data_sub; // 数据总线订阅句柄
    int event_sub_id;                 // 事件总线订阅ID
    pthread_t send_thread;            // 数据发送线程ID
} net_push_srv_ctx_t;

// ==============================================
// 内部函数声明
// ==============================================
static void* _send_thread(void* arg);                            // UDP发送线程
static void _data_bus_cb(data_bus_item_handle_t item, void* user_data);  // 数据总线回调
static void _event_cb(const event_t* event, void* user_data);    // 事件总线回调
static int _udp_send_frame(net_push_srv_ctx_t* ctx, const uint8_t* data, uint32_t w, uint32_t h, uint32_t fmt); // 发送一帧视频

// ==============================================
// 函数：net_push_srv_create
// 功能：创建推流服务，初始化资源、队列
// 参数：config-配置参数 out_handle-输出服务句柄
// 返回：0成功，负数失败
// ==============================================
int net_push_srv_create(const net_push_srv_config_t* config, net_push_srv_handle_t* out_handle) {
    // 【新总线适配】入参校验：校验总线名称（替代老旧句柄）
    if (!config || !out_handle || !config->data_bus_name || !config->event_bus_name) return -1;

    // 分配服务上下文内存
    net_push_srv_ctx_t* ctx = calloc(1, sizeof(net_push_srv_ctx_t));
    if (!ctx) return -1;

    // 复制配置参数
    memcpy(&ctx->config, config, sizeof(net_push_srv_config_t));
    // 分配队列缓冲区（仅1帧指针大小）
    ctx->queue_buffer = malloc(sizeof(void*) * NET_PUSH_MAX_FRAME);
    // 初始化发送队列
    Queue_Init(&ctx->send_queue, ctx->queue_buffer, NET_PUSH_MAX_FRAME);
    // 标记服务创建完成
    ctx->is_created = true;
    // 输出服务句柄给外部
    *out_handle = ctx;

    LOG_I("NetPush: 创建成功(仅缓存最新1帧)，端口:%d", config->bind_port);
    return 0;
}

// ==============================================
// 函数：net_push_srv_start
// 功能：启动推流服务，创建socket、订阅总线、启动发送线程
// ==============================================
int net_push_srv_start(net_push_srv_handle_t handle) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)handle;
    // 校验服务状态
    if (!ctx || !ctx->is_created || ctx->is_running) return -1;

    // 1. 创建UDP套接字
    ctx->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ctx->config.bind_port);  // 绑定端口
    addr.sin_addr.s_addr = INADDR_ANY;             // 监听所有网卡
    bind(ctx->sock_fd, (const struct sockaddr*)&addr, (socklen_t )sizeof(addr));

    // 2. 【新总线适配】订阅数据总线：名称传参，接收VIDEO_FRAME类型数据
    data_bus_subscribe(ctx->config.data_bus_name, DATA_TYPE_VIDEO, _data_bus_cb, ctx, &ctx->data_sub);
    
    // 3. 【新总线适配】订阅事件总线：名称传参，监听SYS_STOP系统停止事件
    event_subscriber_t sub = {
        .event_type = EVENT_TYPE_SYS_STOP,
        .callback = _event_cb,
        .user_data = ctx
    };
    ctx->event_sub_id = event_bus_subscribe(ctx->config.event_bus_name, &sub);

    // 4. 启动UDP发送线程
    ctx->is_running = true;
    pthread_create(&ctx->send_thread, NULL, _send_thread, ctx);

    LOG_I("NetPush: 服务启动，等待客户端连接");
    return 0;
}

// ==============================================
// 函数：net_push_srv_stop
// 功能：停止推流服务
// ==============================================
int net_push_srv_stop(net_push_srv_handle_t handle) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)handle;
    if (!ctx || !ctx->is_running) return -1;

    ctx->is_running = false;
    pthread_join(ctx->send_thread, NULL);
    LOG_I("NetPush: 服务已停止");
    return 0;
}

// ==============================================
// 函数：net_push_srv_destroy
// 功能：销毁推流服务，释放所有资源
// ==============================================
int net_push_srv_destroy(net_push_srv_handle_t handle) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)handle;
    if (!ctx) return 0;

    // 停止线程并等待退出
    ctx->is_running = false;
    pthread_join(ctx->send_thread, NULL);

    // 【新总线适配】取消总线订阅：名称传参
    event_bus_unsubscribe(ctx->config.event_bus_name, ctx->event_sub_id);
    data_bus_unsubscribe(ctx->config.data_bus_name, &ctx->data_sub);
    // 关闭socket
    close(ctx->sock_fd);

    // 释放队列中剩余的数据（引用计数-1）
    void* item = NULL;
    while (Queue_Get(&ctx->send_queue, &item) == 0) {
        data_bus_release(item);
    }

    // 释放内存
    free(ctx->queue_buffer);
    free(ctx);
    LOG_I("NetPush: 销毁完成");
    return 0;
}

// ==============================================
// 函数：_data_bus_cb
// 功能：数据总线回调（核心）
// 逻辑：新帧到来 → 清空旧帧 → 仅保存最新1帧
// 特点：零拷贝，仅传递指针，不复制数据
// ==============================================
static void _data_bus_cb(data_bus_item_handle_t item, void* user_data) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)user_data;
    if (!ctx || !ctx->is_running) return;

    // ==============================================
    // 核心逻辑：实时视频只需要最新帧
    // 新帧到来时，清空队列中所有旧帧（释放引用）
    // ==============================================
    void* old_item = NULL;
    while (Queue_Get(&ctx->send_queue, &old_item) == 0) {
        data_bus_release(old_item);
    }

    // ==============================================
    // 【新总线适配+BUG修复】正确增加引用计数（名称传参）
    // 防止总线自动释放，保证数据发送前有效
    // ==============================================
    data_bus_item_handle_t ref_item = NULL;
    data_bus_acquire_latest(ctx->config.data_bus_name, DATA_TYPE_VIDEO, &ref_item);
    if (ref_item) {
        // 将最新帧入队
        Queue_Put(&ctx->send_queue, ref_item);
    }
}

// ==============================================
// 函数：_event_cb
// 功能：事件总线回调，接收系统停止信号
// ==============================================
static void _event_cb(const event_t* event, void* user_data) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)user_data;
    if (ctx) {
        // 收到停止事件，关闭服务
        ctx->is_running = false;
    }
}

// ==============================================
// 函数：_send_thread
// 功能：UDP发送线程（独立线程，不阻塞总线）
// 流程：等待客户端连接 → 取队列最新帧 → 发送数据
// ==============================================
static void* _send_thread(void* arg) {
    net_push_srv_ctx_t* ctx = (net_push_srv_ctx_t*)arg;

    while (ctx->is_running) {
        // ==============================================
        // 步骤1：等待客户端握手（客户端先发1个字节）
        // ==============================================
        if (!ctx->client_connected) {
            char buf[8];
            struct sockaddr_in client;
            socklen_t len = sizeof(client);
            // 非阻塞接收客户端握手包
            ssize_t ret = recvfrom(ctx->sock_fd, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr*)&client, &len);
            // 收到握手包，记录客户端地址
            if (ret >= 0) {
                memcpy(&ctx->client_addr, &client, sizeof(client));
                ctx->client_connected = true;
                LOG_I("NetPush: 客户端连接成功: %s:%d", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
            }
            usleep(10000);
            continue;
        }

        // ==============================================
        // 步骤2：从队列取出最新1帧
        // ==============================================
        data_bus_item_handle_t item = NULL;
        if (Queue_Get(&ctx->send_queue, (void**)&item) != 0) {
            usleep(1000);
            continue;
        }

        // ==============================================
        // 步骤3：获取数据指针（只读，零拷贝）
        // ==============================================
        const void* data = data_bus_get_readonly_ptr(item);
        // ==============================================
        // 步骤4：发送视频帧（YUYV格式 640x360）
        // ==============================================
        _udp_send_frame(ctx, data, 640, 360, FRAME_FMT_YUYV);
        
        // ==============================================
        // 步骤5：发送完成，释放数据引用
        // ==============================================
        data_bus_release(item);
    }
    return NULL;
}

// ==============================================
// 函数：_udp_send_frame
// 功能：发送一帧视频数据（帧头+分包数据）
// ==============================================
static int _udp_send_frame(net_push_srv_ctx_t* ctx, const uint8_t* data, uint32_t w, uint32_t h, uint32_t fmt) {
    // 构造视频帧头
    net_frame_header_t hdr = {
        .magic = NET_FRAME_MAGIC,
        .frame_size = w * h * 2,  // YUYV格式：1像素占2字节
        .width = w,
        .height = h,
        .format = fmt,
        .timestamp = 0
    };

    // 1. 先发送帧头（客户端先解析帧头）
    sendto(ctx->sock_fd, &hdr, sizeof(hdr), 0, (struct sockaddr*)&ctx->client_addr, sizeof(ctx->client_addr));
    
    // 2. 分包发送视频数据（MTU=1400，避免丢包）
    size_t total = hdr.frame_size;
    size_t offset = 0;
    while (offset < total) {
        size_t send_len = (total - offset) > UDP_MTU_SIZE ? UDP_MTU_SIZE : (total - offset);
        sendto(ctx->sock_fd, data + offset, send_len, 0, (struct sockaddr*)&ctx->client_addr, sizeof(ctx->client_addr));
        offset += send_len;
    }
    return 0;
}