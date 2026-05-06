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
// 内部宏定义
// ==========================================================================
#define EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT 32
#define EVENT_BUS_MAX_QUEUE_EVENTS 256

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
// 事件总线上下文
// ==========================================================================
typedef struct {
    event_bus_config_t config;
    subscriber_entry_t *subscribers;
    uint32_t subscriber_count;
    uint32_t max_subscribers;
    int next_subscription_id;
    pthread_mutex_t lock;
    pthread_rwlock_t rwlock;

    int pipefd[2];
    Queue_t event_queue;
    void *queue_buffer[EVENT_BUS_MAX_QUEUE_EVENTS];
} event_bus_context_t;

// ==========================================================================
// ✅ 优化1：事件类型字符串映射表（真正使用，删除 unused）
// 仅映射核心通用事件，扩展事件走区间匹配
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
static void _event_bus_free_event(event_t *event);

// ==========================================================================
// ✅ 优化2：重写 事件类型转字符串（精准+兼容）
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
// 对外API实现（日志全部优化为打印事件名称）
// ==========================================================================
int event_bus_init(const event_bus_config_t *config,
                   event_bus_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    event_bus_context_t *ctx = (event_bus_context_t*)malloc(sizeof(event_bus_context_t));
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(event_bus_context_t));

    memcpy(&ctx->config, config, sizeof(event_bus_config_t));
    ctx->max_subscribers = (config->max_subscribers > 0) ? config->max_subscribers : EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->next_subscription_id = 1;

    ctx->subscribers = (subscriber_entry_t*)malloc(ctx->max_subscribers * sizeof(subscriber_entry_t));
    if (!ctx->subscribers) {
        free(ctx);
        return -1;
    }
    memset(ctx->subscribers, 0, ctx->max_subscribers * sizeof(subscriber_entry_t));

    pthread_mutex_init(&ctx->lock, NULL);
    pthread_rwlock_init(&ctx->rwlock, NULL);

    if (pipe(ctx->pipefd) != 0) {
        LOG_E("Event Bus: Failed to create pipe");
        free(ctx->subscribers);
        free(ctx);
        return -1;
    }

    Queue_Init(&ctx->event_queue, ctx->queue_buffer, EVENT_BUS_MAX_QUEUE_EVENTS);

    *out_handle = (event_bus_handle_t)ctx;
    LOG_I("Event Bus: Initialized (Async Mode), max subscribers=%u", ctx->max_subscribers);
    return 0;
}

int event_bus_subscribe(event_bus_handle_t handle,
                        const event_subscriber_t *subscriber)
{
    if (!handle || !subscriber || !subscriber->callback) {
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

    // ✅ 优化3：打印事件名称，而非数字
    LOG_I("Event Bus: Subscribed (id=%d, event=%s)", 
          id, event_type_to_str(subscriber->event_type));
    return id;
}

int event_bus_unsubscribe(event_bus_handle_t handle, int subscription_id)
{
    if (!handle || subscription_id <= 0) {
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

int event_bus_publish(event_bus_handle_t handle, const event_t *event)
{
    if (!handle || !event || event->type == EVENT_TYPE_INVALID) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    event_t *event_copy = (event_t*)malloc(sizeof(event_t));
    if (!event_copy) {
        LOG_E("Event Bus: Out of memory");
        return -1;
    }
    memcpy(event_copy, event, sizeof(event_t));

    if (event_copy->timestamp == 0) {
        event_copy->timestamp = _event_bus_get_timestamp_us();
    }

    if (Queue_Put(&ctx->event_queue, event_copy) != QUEUE_OK) {
        // ✅ 优化4：打印丢弃的事件名称
        LOG_W("Event Bus: Queue full, drop event: %s", event_type_to_str(event->type));
        free(event_copy);
        return -1;
    }

    const char wakeup_byte = 0x01;
    write(ctx->pipefd[1], &wakeup_byte, 1);

    return 0;
}

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

int event_bus_get_wait_fd(event_bus_handle_t handle)
{
    if (!handle) return -1;
    event_bus_context_t *ctx = (event_bus_context_t*)handle;
    return ctx->pipefd[0];
}

int event_bus_dispatch(event_bus_handle_t handle)
{
    if (!handle) return -1;
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    char buf[32];
    read(ctx->pipefd[0], buf, sizeof(buf));

    event_t *event = NULL;
    if (Queue_Get(&ctx->event_queue, (void**)&event) != QUEUE_OK) {
        return -1;
    }
    if (!event) return -1;

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

    // 优化5：分发时打印事件（调试专用，可开关）
    // LOG_D("Event Bus: Dispatch event: %s", event_type_to_str(event->type));

    for (int i = 0; i < temp_count; i++) {
        if (temp_callbacks[i].cb) {
            temp_callbacks[i].cb(event, temp_callbacks[i].user_data);
        }
    }

    _event_bus_free_event(event);
    return 0;
}

int event_bus_deinit(event_bus_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    close(ctx->pipefd[0]);
    close(ctx->pipefd[1]);

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

// ==========================================================================
// 内部辅助函数
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