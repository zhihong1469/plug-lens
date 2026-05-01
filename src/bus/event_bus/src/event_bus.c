// src/bus/event_bus/src/event_bus.c
#include "event_bus.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT 32

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
// 内部上下文
// ==========================================================================
typedef struct {
    event_bus_config_t config;
    subscriber_entry_t *subscribers;
    uint32_t subscriber_count;
    uint32_t max_subscribers;
    int next_subscription_id;
    pthread_mutex_t lock;
    pthread_rwlock_t rwlock; // 读写锁：读（订阅者遍历）共享，写（注册/注销）互斥
} event_bus_context_t;

// ==========================================================================
// 字符串映射表（简化版）
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

    *out_handle = (event_bus_handle_t)ctx;
    LOG_I("Event Bus: Initialized, max subscribers=%u", ctx->max_subscribers);
    return 0;
}

int event_bus_subscribe(event_bus_handle_t handle,
                        const event_subscriber_t *subscriber)
{
    if (handle == NULL || subscriber == NULL || subscriber->callback == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    pthread_rwlock_wrlock(&ctx->rwlock); // 写锁：注册是写操作

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

int event_bus_publish(event_bus_handle_t handle, const event_t *event)
{
    if (handle == NULL || event == NULL || event->type == EVENT_TYPE_INVALID) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    // 填充时间戳（如果未设置）
    event_t local_event = *event;
    if (local_event.timestamp == 0) {
        local_event.timestamp = _event_bus_get_timestamp_us();
    }

    LOG_D("Event Bus: Publishing event %s (source=%s)", 
          event_type_to_str(local_event.type), 
          local_event.source ? local_event.source : "unknown");

    // 【核心】使用读锁遍历订阅者（读锁是共享的，不阻塞其他发布）
    pthread_rwlock_rdlock(&ctx->rwlock);

    // 先遍历一遍，把需要通知的回调和数据暂存起来（在锁外执行回调）
    // 这是为了避免：
    // 1. 回调里再次调用 bus 接口导致死锁
    // 2. 持有锁的时间过长
    #define MAX_TEMP_CALLBACKS 32
    struct {
        event_callback_t cb;
        void *user_data;
    } temp_callbacks[MAX_TEMP_CALLBACKS];
    int temp_count = 0;

    for (uint32_t i = 0; i < ctx->max_subscribers && temp_count < MAX_TEMP_CALLBACKS; i++) {
        if (!ctx->subscribers[i].valid) continue;

        // 检查是否匹配：订阅者订阅了该类型，或者订阅了所有类型（EVENT_TYPE_INVALID）
        if (ctx->subscribers[i].event_type == EVENT_TYPE_INVALID ||
            ctx->subscribers[i].event_type == local_event.type) {
            temp_callbacks[temp_count].cb = ctx->subscribers[i].callback;
            temp_callbacks[temp_count].user_data = ctx->subscribers[i].user_data;
            temp_count++;
        }
    }

    pthread_rwlock_unlock(&ctx->rwlock);

    // 【关键】在锁外执行回调
    for (int i = 0; i < temp_count; i++) {
        if (temp_callbacks[i].cb != NULL) {
            temp_callbacks[i].cb(&local_event, temp_callbacks[i].user_data);
        }
    }

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

int event_bus_deinit(event_bus_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

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
    // 简化版，实际项目中可以完善
    if (type >= EVENT_TYPE_CUSTOM_BASE) return "CUSTOM";
    if (type >= EVENT_TYPE_DISP_BASE) return "DISP";
    if (type >= EVENT_TYPE_AI_BASE) return "AI";
    if (type >= EVENT_TYPE_CAP_BASE) return "CAP";
    if (type >= EVENT_TYPE_MOD_BASE) return "MOD";
    if (type >= EVENT_TYPE_SYS_BASE) return "SYS";
    return "UNKNOWN";
}

static uint64_t _event_bus_get_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}