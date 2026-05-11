// src/bus/event_bus/src/event_bus.c
#include "event_bus.h"
#include "log.h"
#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

// ==========================================================================
// 内部宏定义（默认配置，外部不配置时使用）
// ==========================================================================
#define EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT 32    // 默认最大订阅者数
#define EVENT_BUS_MAX_QUEUE_EVENTS 256          // 事件队列最大缓存256个事件

// ==========================================================================
// 内部订阅者条目结构体
// 存储单个订阅者的完整信息（总线内部使用，外部不可见）
// ==========================================================================
typedef struct {
    int id;                     // 订阅唯一ID（自动生成）
    event_type_t event_type;    // 订阅的事件类型
    event_callback_t callback;  // 事件回调函数
    void *user_data;            // 用户自定义数据
    bool valid;                 // 标记：是否是有效订阅
} subscriber_entry_t;

// ==========================================================================
// 事件总线上下文（总控结构体）
// 事件总线所有资源、状态、锁、队列、管道都存在这里
// 句柄 event_bus_handle_t = 该结构体的指针
// ==========================================================================
typedef struct {
    event_bus_config_t config;          // 外部传入的配置
    subscriber_entry_t *subscribers;    // 订阅者数组
    uint32_t subscriber_count;          // 当前有效订阅者数量
    uint32_t max_subscribers;           // 最大支持订阅者数
    int next_subscription_id;           // 下一个订阅ID（自增）
    pthread_mutex_t lock;               // 互斥锁（保护总线结构修改）
    pthread_rwlock_t rwlock;            // 读写锁（保护订阅者遍历，多读少写）

    int pipefd[2];                      // 管道：[0]读端 [1]写端 → 异步唤醒主线程
    Queue_t event_queue;                // 事件队列：缓存待分发的事件
    void *queue_buffer[EVENT_BUS_MAX_QUEUE_EVENTS]; // 队列缓存空间
} event_bus_context_t;

// ==========================================================================
// 事件类型字符串映射表（日志打印专用）
// ==========================================================================
static const char* g_event_type_str[] = {
    [EVENT_TYPE_INVALID] = "INVALID",
    [EVENT_TYPE_SYS_STATE_CHANGED] = "SYS_STATE_CHANGED",
    [EVENT_TYPE_SYS_START] = "SYS_START",
    [EVENT_TYPE_SYS_STOP] = "SYS_STOP",
    [EVENT_TYPE_MOD_STATE_CHANGED] = "MOD_STATE_CHANGED",
    [EVENT_TYPE_CAP_FRAME_READY] = "CAP_FRAME_READY",
};

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static uint64_t _event_bus_get_timestamp_us(void);  // 获取微秒级时间戳
static void _event_bus_free_event(event_t *event);  // 释放事件内存

// ==========================================================================
// 事件类型转字符串（兼容所有事件，日志打印友好）
// ==========================================================================
const char* event_type_to_str(event_type_t type)
{
    // 优先使用精准映射表
    if (type < sizeof(g_event_type_str)/sizeof(char*) && g_event_type_str[type]) {
        return g_event_type_str[type];
    }

    // 扩展事件按区间分类（兼容原有设计）
    if (type >= EVENT_TYPE_CUSTOM_BASE) return "CUSTOM_EVENT";
    if (type >= EVENT_TYPE_DISP_BASE)   return "DISP_EVENT";
    if (type >= EVENT_TYPE_AI_BASE)     return "AI_EVENT";
    if (type >= EVENT_TYPE_CAP_BASE)    return "CAP_EVENT";
    if (type >= EVENT_TYPE_MOD_BASE)    return "MOD_EVENT";
    if (type >= EVENT_TYPE_SYS_BASE)    return "SYS_EVENT";
    
    return "UNKNOWN_EVENT";
}

// ==========================================================================
// 对外API实现
// ==========================================================================

/**
 * @brief 初始化事件总线
 * 核心工作：申请内存、初始化锁、创建管道、初始化事件队列
 */
int event_bus_init(const event_bus_config_t *config,
                   event_bus_handle_t *out_handle)
{
    // 参数校验
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    // 申请总线上下文内存
    event_bus_context_t *ctx = (event_bus_context_t*)malloc(sizeof(event_bus_context_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(event_bus_context_t));

    // 加载配置
    memcpy(&ctx->config, config, sizeof(event_bus_config_t));
    ctx->max_subscribers = (config->max_subscribers > 0) ? config->max_subscribers : EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->next_subscription_id = 1;  // 订阅ID从1开始

    // 申请订阅者数组内存
    ctx->subscribers = (subscriber_entry_t*)malloc(ctx->max_subscribers * sizeof(subscriber_entry_t));
    if (!ctx->subscribers) {
        free(ctx);
        return -1;
    }
    memset(ctx->subscribers, 0, ctx->max_subscribers * sizeof(subscriber_entry_t));

    // 初始化线程锁
    pthread_mutex_init(&ctx->lock, NULL);
    pthread_rwlock_init(&ctx->rwlock, NULL);

    // 创建管道（核心：异步唤醒主线程分发事件）
    if (pipe(ctx->pipefd) != 0) {
        LOG_E("Event Bus: Failed to create pipe");
        free(ctx->subscribers);
        free(ctx);
        return -1;
    }

    // 初始化事件队列（缓存发布的事件）
    Queue_Init(&ctx->event_queue, ctx->queue_buffer, EVENT_BUS_MAX_QUEUE_EVENTS);

    // 输出总线句柄
    *out_handle = (event_bus_handle_t)ctx;
    LOG_I("Event Bus: Initialized (Async Mode), max subscribers=%u", ctx->max_subscribers);
    return 0;
}

/**
 * @brief 订阅事件
 * 向总线注册回调，指定事件触发时执行
 */
int event_bus_subscribe(event_bus_handle_t handle,
                        const event_subscriber_t *subscriber)
{
    // 参数校验
    if (!handle || !subscriber || !subscriber->callback) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    // 写锁：修改订阅者列表
    pthread_rwlock_wrlock(&ctx->rwlock);

    int id = -1;
    // 遍历订阅者数组，找空位注册
    for (uint32_t i = 0; i < ctx->max_subscribers; i++) {
        if (!ctx->subscribers[i].valid) {
            id = ctx->next_subscription_id++;
            ctx->subscribers[i].id = id;
            ctx->subscribers[i].event_type = subscriber->event_type;
            ctx->subscribers[i].callback = subscriber->callback;
            ctx->subscribers[i].user_data = subscriber->user_data;
            ctx->subscribers[i].valid = true;
            ctx->subscriber_count++;
            break;
        }
    }

    pthread_rwlock_unlock(&ctx->rwlock);

    // 无空位，订阅失败
    if (id < 0) {
        LOG_E("Event Bus: No more subscriber slots");
        return -1;
    }
    LOG_I("Event Bus: Subscribed (id=%d, event=%s)", 
          id, event_type_to_str(subscriber->event_type));
    return id;
}

/**
 * @brief 取消订阅事件
 * 根据订阅ID失效对应的订阅者
 */
int event_bus_unsubscribe(event_bus_handle_t handle, int subscription_id)
{
    // 参数校验
    if (!handle || subscription_id <= 0) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    // 写锁：修改订阅者列表
    pthread_rwlock_wrlock(&ctx->rwlock);
    int ret = -1;
    // 遍历查找对应ID的订阅者
    for (uint32_t i = 0; i < ctx->max_subscribers; i++) {
        if (ctx->subscribers[i].valid && ctx->subscribers[i].id == subscription_id) {
            ctx->subscribers[i].valid = false;
            ctx->subscriber_count--;
            ret = 0;
            break;
        }
    }
    pthread_rwlock_unlock(&ctx->rwlock);

    if (ret == 0) {
        LOG_I("Event Bus: Unsubscribed (id=%d)", subscription_id);
    }
    return ret;
}

/**
 * @brief 发布事件（异步）
 * 1. 拷贝事件
 * 2. 放入事件队列
 * 3. 写管道唤醒主线程分发
 */
int event_bus_publish(event_bus_handle_t handle, const event_t *event)
{
    // 参数校验
    if (!handle || !event || event->type == EVENT_TYPE_INVALID) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    // 申请内存，拷贝事件（保证线程安全）
    event_t *event_copy = (event_t*)malloc(sizeof(event_t));
    if (!event_copy) {
        LOG_E("Event Bus: Out of memory");
        return -1;
    }
    memcpy(event_copy, event, sizeof(event_t));

    // 自动填充时间戳
    if (event_copy->timestamp == 0) {
        event_copy->timestamp = _event_bus_get_timestamp_us();
    }

    // 将事件放入队列
    if (Queue_Put(&ctx->event_queue, event_copy) != QUEUE_OK) {
        LOG_W("Event Bus: Queue full, drop event: %s", event_type_to_str(event->type));
        free(event_copy);
        return -1;
    }

    // 写管道：唤醒主线程（select/poll监听到读端可读）
    const char wakeup_byte = 0x01;
    write(ctx->pipefd[1], &wakeup_byte, 1);

    return 0;
}

/**
 * @brief 快速发布简单事件（简化接口）
 */
int event_bus_publish_simple(event_bus_handle_t handle,
                             event_type_t type, const char *source)
{
    event_t evt = {0};
    evt.type = type;
    evt.priority = EVENT_PRIORITY_NORMAL;
    evt.source = source;
    evt.timestamp = 0;
    return event_bus_publish(handle, &evt);
}

/**
 * @brief 获取管道读端FD（用于主线程异步监听）
 */
int event_bus_get_wait_fd(event_bus_handle_t handle)
{
    if (!handle) return -1;
    event_bus_context_t *ctx = (event_bus_context_t*)handle;
    return ctx->pipefd[0];
}

/**
 * @brief 分发事件（主线程核心函数）
 * 1. 读取管道清空唤醒信号
 * 2. 从队列取出事件
 * 3. 遍历订阅者，执行匹配的回调
 * 4. 释放事件内存
 */
int event_bus_dispatch(event_bus_handle_t handle)
{
    if (!handle) return -1;
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    // 读取管道：清空唤醒信号
    char buf[32];
    read(ctx->pipefd[0], buf, sizeof(buf));

    // 从队列取出一个待处理的事件
    event_t *event = NULL;
    if (Queue_Get(&ctx->event_queue, (void**)&event) != QUEUE_OK) {
        return -1;
    }
    if (!event) return -1;

    // 读锁：遍历订阅者列表（多读安全）
    pthread_rwlock_rdlock(&ctx->rwlock);

    // 临时缓存回调函数（锁外执行，避免死锁）
    #define MAX_TEMP_CALLBACKS 32
    struct {
        event_callback_t cb;
        void *user_data;
    } temp_callbacks[MAX_TEMP_CALLBACKS];
    int temp_count = 0;

    // 遍历所有订阅者，匹配事件类型
    for (uint32_t i = 0; i < ctx->max_subscribers && temp_count < MAX_TEMP_CALLBACKS; i++) {
        if (!ctx->subscribers[i].valid) continue;

        // 匹配：订阅所有事件 或 订阅当前事件
        if (ctx->subscribers[i].event_type == EVENT_TYPE_INVALID ||
            ctx->subscribers[i].event_type == event->type) {
            temp_callbacks[temp_count].cb = ctx->subscribers[i].callback;
            temp_callbacks[temp_count].user_data = ctx->subscribers[i].user_data;
            temp_count++;
        }
    }

    pthread_rwlock_unlock(&ctx->rwlock);

    // 【关键】锁外执行回调，避免锁阻塞
    for (int i = 0; i < temp_count; i++) {
        if (temp_callbacks[i].cb) {
            temp_callbacks[i].cb(event, temp_callbacks[i].user_data);
        }
    }

    // 释放事件内存
    _event_bus_free_event(event);
    return 0;
}

/**
 * @brief 销毁事件总线，释放所有资源
 */
int event_bus_deinit(event_bus_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    // 关闭管道
    close(ctx->pipefd[0]);
    close(ctx->pipefd[1]);

    // 清空队列，释放所有未分发的事件
    event_t *event = NULL;
    while (Queue_Get(&ctx->event_queue, (void**)&event) == QUEUE_OK) {
        _event_bus_free_event(event);
    }

    // 释放订阅者数组
    pthread_rwlock_wrlock(&ctx->rwlock);
    free(ctx->subscribers);
    ctx->subscribers = NULL;
    ctx->subscriber_count = 0;
    pthread_rwlock_unlock(&ctx->rwlock);

    // 销毁锁
    pthread_rwlock_destroy(&ctx->rwlock);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);

    LOG_I("Event Bus: Deinitialized");
    return 0;
}

// ==========================================================================
// 内部辅助函数
// ==========================================================================

// 获取系统当前微秒级时间戳
static uint64_t _event_bus_get_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// 释放事件内存
static void _event_bus_free_event(event_t *event)
{
    if (event) {
        free(event);
    }
}