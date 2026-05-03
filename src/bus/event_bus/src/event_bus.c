// src/bus/event_bus/src/event_bus.c
#include "event_bus.h"
#include "log.h"
#include "queue.h" // 【新增】引入我们现成的队列库
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h> // 【新增】用于 pipe

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT 32
#define EVENT_BUS_MAX_QUEUE_EVENTS 256    // 【新增】内部队列最大深度

// ==========================================================================
// 内部订阅者条目
// ==========================================================================
typedef struct {
    int id;
    event_type_t event_type;
    event_callback_t callback;
    void *user_data;
    bool valid;
} subscriber_entry_t;

// ==========================================================================
// 【核心修改】内部上下文结构体
// ==========================================================================
typedef struct {
    event_bus_config_t config;
    subscriber_entry_t *subscribers;
    uint32_t subscriber_count;
    uint32_t max_subscribers;
    int next_subscription_id;
    pthread_mutex_t lock;
    pthread_rwlock_t rwlock;

    // 【新增】异步化组件
    int pipefd[2];                      // 管道：[0]读端, [1]写端
    Queue_t event_queue;                // 事件队列
    void *queue_buffer[EVENT_BUS_MAX_QUEUE_EVENTS]; // 队列存储区 (存 event_t*)
} event_bus_context_t;

// ==========================================================================
// 字符串映射表
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
static uint64_t _event_bus_get_timestamp_us(void);
static void _event_bus_free_event(event_t *event); // 【新增】辅助释放

// ==========================================================================
// 对外API实现
// ==========================================================================

int event_bus_init(const event_bus_config_t *config,
                   event_bus_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    event_bus_context_t *ctx = (event_bus_context_t*)malloc(sizeof(event_bus_context_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(event_bus_context_t));

    memcpy(&ctx->config, config, sizeof(event_bus_config_t));
    ctx->max_subscribers = (config->max_subscribers > 0) ? config->max_subscribers : EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->next_subscription_id = 1;

    ctx->subscribers = (subscriber_entry_t*)malloc(ctx->max_subscribers * sizeof(subscriber_entry_t));
    if (ctx->subscribers == NULL) {
        free(ctx);
        return -1;
    }
    memset(ctx->subscribers, 0, ctx->max_subscribers * sizeof(subscriber_entry_t));

    pthread_mutex_init(&ctx->lock, NULL);
    pthread_rwlock_init(&ctx->rwlock, NULL);

    // -------------------------------------------------------------------------
    // 【新增】初始化 Pipe
    // -------------------------------------------------------------------------
    if (pipe(ctx->pipefd) != 0) {
        LOG_E("Event Bus: Failed to create pipe");
        free(ctx->subscribers);
        free(ctx);
        return -1;
    }

    // -------------------------------------------------------------------------
    // 【新增】初始化内部队列
    // -------------------------------------------------------------------------
    Queue_Init(&ctx->event_queue, ctx->queue_buffer, EVENT_BUS_MAX_QUEUE_EVENTS);

    *out_handle = (event_bus_handle_t)ctx;
    LOG_I("Event Bus: Initialized (Async Mode), max subscribers=%u", ctx->max_subscribers);
    return 0;
}

int event_bus_subscribe(event_bus_handle_t handle,
                        const event_subscriber_t *subscriber)
{
    // (这部分逻辑保持不变，直接复用原有代码)
    if (handle == NULL || subscriber == NULL || subscriber->callback == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    pthread_rwlock_wrlock(&ctx->rwlock);

    int id = -1;
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

    if (id < 0) {
        LOG_E("Event Bus: No more subscriber slots");
        return -1;
    }

    LOG_I("Event Bus: Subscribed (id=%d, event_type=0x%X)", id, subscriber->event_type);
    return id;
}

int event_bus_unsubscribe(event_bus_handle_t handle,
                          int subscription_id)
{
    // (这部分逻辑保持不变)
    if (handle == NULL || subscription_id <= 0) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    pthread_rwlock_wrlock(&ctx->rwlock);

    int ret = -1;
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

// ==========================================================================
// 【核心修改】Publish 逻辑：只入队，不执行回调
// ==========================================================================
int event_bus_publish(event_bus_handle_t handle, const event_t *event)
{
    if (handle == NULL || event == NULL || event->type == EVENT_TYPE_INVALID) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    // 1. 分配内存拷贝事件 (因为要入队，必须脱离原调用者的栈)
    event_t *event_copy = (event_t*)malloc(sizeof(event_t));
    if (event_copy == NULL) {
        LOG_E("Event Bus: Out of memory for event copy");
        return -1;
    }
    memcpy(event_copy, event, sizeof(event_t));
    
    // 填充时间戳
    if (event_copy->timestamp == 0) {
        event_copy->timestamp = _event_bus_get_timestamp_us();
    }

    // 2. 尝试入队
    if (Queue_Put(&ctx->event_queue, event_copy) != QUEUE_OK) {
        LOG_W("Event Bus: Queue full, dropping event");
        free(event_copy); // 队列满了，丢弃
        return -1;
    }

    // 3. 【关键】写入 Pipe，唤醒主线程
    const char wakeup_byte = 0x01;
    write(ctx->pipefd[1], &wakeup_byte, 1); // 这里不处理错误，尽力而为

    // LOG_D("Event Bus: Event queued (async)"); 
    return 0;
}

int event_bus_publish_simple(event_bus_handle_t handle,
                             event_type_t type,
                             const char *source)
{
    event_t evt = {0};
    evt.type = type;
    evt.priority = EVENT_PRIORITY_NORMAL;
    evt.source = source;
    evt.timestamp = 0;
    return event_bus_publish(handle, &evt);
}

// ==========================================================================
// 【新增 API】获取等待 FD
// ==========================================================================
int event_bus_get_wait_fd(event_bus_handle_t handle)
{
    if (handle == NULL) return -1;
    event_bus_context_t *ctx = (event_bus_context_t*)handle;
    return ctx->pipefd[0]; // 返回读端
}

// ==========================================================================
// 【新增 API】分发事件 (主线程调用)
// ==========================================================================
int event_bus_dispatch(event_bus_handle_t handle)
{
    if (handle == NULL) return -1;
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    // 1. 先把 Pipe 里的数据读空 (防止电平触发导致的一直唤醒)
    char buf[32];
    read(ctx->pipefd[0], buf, sizeof(buf)); 

    // 2. 从队列取出事件
    event_t *event = NULL;
    if (Queue_Get(&ctx->event_queue, (void**)&event) != QUEUE_OK) {
        return -1; // 没事件
    }

    if (event == NULL) return -1;

    // 3. 【核心】执行回调 (逻辑复用旧代码，但在主线程执行)
    // 在持有 rdlock 时调用用户回调。如果用户回调内部尝试获取写锁或其他锁，可能导致死锁。
    pthread_rwlock_rdlock(&ctx->rwlock);

    #define MAX_TEMP_CALLBACKS 32
    struct {
        event_callback_t cb;
        void *user_data;
    } temp_callbacks[MAX_TEMP_CALLBACKS];
    int temp_count = 0;

    for (uint32_t i = 0; i < ctx->max_subscribers && temp_count < MAX_TEMP_CALLBACKS; i++) {
        if (!ctx->subscribers[i].valid) continue;

        if (ctx->subscribers[i].event_type == EVENT_TYPE_INVALID ||
            ctx->subscribers[i].event_type == event->type) {
            temp_callbacks[temp_count].cb = ctx->subscribers[i].callback;
            temp_callbacks[temp_count].user_data = ctx->subscribers[i].user_data;
            temp_count++;
        }
    }

    pthread_rwlock_unlock(&ctx->rwlock);

    // 执行回调
    for (int i = 0; i < temp_count; i++) {
        if (temp_callbacks[i].cb != NULL) {
            temp_callbacks[i].cb(event, temp_callbacks[i].user_data);
        }
    }

    // 4. 释放事件内存
    _event_bus_free_event(event);

    return 0;
}

int event_bus_deinit(event_bus_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    // 【新增】关闭 Pipe
    close(ctx->pipefd[0]);
    close(ctx->pipefd[1]);

    // 【新增】清空队列并释放内存
    event_t *event = NULL;
    while (Queue_Get(&ctx->event_queue, (void**)&event) == QUEUE_OK) {
        _event_bus_free_event(event);
    }

    pthread_rwlock_wrlock(&ctx->rwlock);
    free(ctx->subscribers);
    ctx->subscribers = NULL;
    ctx->subscriber_count = 0;
    pthread_rwlock_unlock(&ctx->rwlock);

    pthread_rwlock_destroy(&ctx->rwlock);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);

    LOG_I("Event Bus: Deinitialized");
    return 0;
}

const char* event_type_to_str(event_type_t type)
{
    if (type >= EVENT_TYPE_CUSTOM_BASE) return "CUSTOM";
    if (type >= EVENT_TYPE_DISP_BASE) return "DISP";
    if (type >= EVENT_TYPE_AI_BASE) return "AI";
    if (type >= EVENT_TYPE_CAP_BASE) return "CAP";
    if (type >= EVENT_TYPE_MOD_BASE) return "MOD";
    if (type >= EVENT_TYPE_SYS_BASE) return "SYS";
    return "UNKNOWN";
}

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

static uint64_t _event_bus_get_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void _event_bus_free_event(event_t *event)
{
    if (event) {
        free(event);
    }
}
