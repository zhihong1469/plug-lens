// src/bus/event_bus/src/event_bus.c
#include "event_bus.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define MAX_SUBSCRIBERS 64
#define MAX_EVENT_QUEUE 256

// ==========================================================================
// 订阅者结构体
// ==========================================================================
typedef struct {
    event_type_t type;
    event_callback_t cb;
    void *user_data;
} subscriber_t;

// ==========================================================================
// 内部上下文结构体
// ==========================================================================
typedef struct {
    subscriber_t subscribers[MAX_SUBSCRIBERS];
    uint32_t subscriber_count;
    event_bus_stats_t stats;
    bool enable_stats;
    pthread_mutex_t lock;
} event_bus_context_t;

// ==========================================================================
// 事件/优先级字符串映射表
// ==========================================================================
static const char* g_event_type_str[] = {
    [EVENT_TYPE_INVALID] = "INVALID",
    [EVENT_TYPE_SYSTEM_INIT] = "SYSTEM_INIT",
    [EVENT_TYPE_SYSTEM_START] = "SYSTEM_START",
    [EVENT_TYPE_SYSTEM_STOP] = "SYSTEM_STOP",
    [EVENT_TYPE_SYSTEM_SHUTDOWN] = "SYSTEM_SHUTDOWN",
    [EVENT_TYPE_CAPTURE_START] = "CAPTURE_START",
    [EVENT_TYPE_CAPTURE_STOP] = "CAPTURE_STOP",
    [EVENT_TYPE_CAPTURE_FRAME_READY] = "CAPTURE_FRAME_READY",
    [EVENT_TYPE_AI_START] = "AI_START",
    [EVENT_TYPE_AI_STOP] = "AI_STOP",
    [EVENT_TYPE_AI_RESULT_READY] = "AI_RESULT_READY",
    [EVENT_TYPE_DISPLAY_START] = "DISPLAY_START",
    [EVENT_TYPE_DISPLAY_STOP] = "DISPLAY_STOP",
    [EVENT_TYPE_STORAGE_START] = "STORAGE_START",
    [EVENT_TYPE_STORAGE_STOP] = "STORAGE_STOP",
    [EVENT_TYPE_ERROR] = "ERROR",
    [EVENT_TYPE_WARNING] = "WARNING",
};

static const char* g_event_priority_str[] = {
    [EVENT_PRIORITY_LOW] = "LOW",
    [EVENT_PRIORITY_NORMAL] = "NORMAL",
    [EVENT_PRIORITY_HIGH] = "HIGH",
    [EVENT_PRIORITY_CRITICAL] = "CRITICAL",
};

// ==========================================================================
// 内部辅助函数
// ==========================================================================
static uint64_t _event_bus_get_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// ==========================================================================
// 对外API实现
// ==========================================================================

const char* event_type_to_str(event_type_t type)
{
    if (type < 0 || type >= EVENT_TYPE_MAX) {
        return "CUSTOM";
    }
    return g_event_type_str[type];
}

const char* event_priority_to_str(event_priority_t priority)
{
    if (priority < 0 || priority >= EVENT_PRIORITY_MAX) {
        return "UNKNOWN";
    }
    return g_event_priority_str[priority];
}

int event_bus_init(const event_bus_config_t *config, event_bus_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return -1;
    }

    event_bus_context_t *ctx = (event_bus_context_t*)malloc(sizeof(event_bus_context_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(event_bus_context_t));

    ctx->enable_stats = config ? config->enable_stats : true;
    ctx->subscriber_count = 0;
    memset(&ctx->stats, 0, sizeof(ctx->stats));

    pthread_mutex_init(&ctx->lock, NULL);

    *out_handle = (event_bus_handle_t)ctx;
    return 0;
}

int event_bus_subscribe(event_bus_handle_t handle,
                         event_type_t type,
                         event_callback_t cb,
                         void *user_data)
{
    if (handle == NULL || cb == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    if (ctx->subscriber_count >= MAX_SUBSCRIBERS) {
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    // 添加订阅者
    ctx->subscribers[ctx->subscriber_count].type = type;
    ctx->subscribers[ctx->subscriber_count].cb = cb;
    ctx->subscribers[ctx->subscriber_count].user_data = user_data;
    ctx->subscriber_count++;

    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

int event_bus_unsubscribe(event_bus_handle_t handle,
                           event_type_t type,
                           event_callback_t cb)
{
    if (handle == NULL || cb == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    for (uint32_t i = 0; i < ctx->subscriber_count; i++) {
        if (ctx->subscribers[i].type == type && ctx->subscribers[i].cb == cb) {
            // 前移覆盖
            for (uint32_t j = i; j < ctx->subscriber_count - 1; j++) {
                ctx->subscribers[j] = ctx->subscribers[j + 1];
            }
            ctx->subscriber_count--;
            break;
        }
    }
    pthread_mutex_unlock(&ctx->lock);

    return 0;
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

    pthread_mutex_lock(&ctx->lock);

    // 更新统计
    if (ctx->enable_stats) {
        ctx->stats.total_published++;
        if (local_event.type < EVENT_TYPE_MAX) {
            ctx->stats.event_count[local_event.type]++;
        }
    }

    // 投递事件给所有匹配的订阅者
    uint32_t delivered = 0;
    for (uint32_t i = 0; i < ctx->subscriber_count; i++) {
        if (ctx->subscribers[i].type == local_event.type 
            || ctx->subscribers[i].type == EVENT_TYPE_INVALID) { // 订阅所有事件
            ctx->subscribers[i].cb(&local_event, ctx->subscribers[i].user_data);
            delivered++;
        }
    }

    // 更新统计
    if (ctx->enable_stats) {
        ctx->stats.total_delivered += delivered;
        if (delivered == 0 && ctx->subscriber_count > 0) {
            ctx->stats.total_dropped++;
        }
    }

    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

int event_bus_get_stats(event_bus_handle_t handle, event_bus_stats_t *stats)
{
    if (handle == NULL || stats == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    memcpy(stats, &ctx->stats, sizeof(event_bus_stats_t));
    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

int event_bus_reset_stats(event_bus_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

int event_bus_deinit(event_bus_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    event_bus_context_t *ctx = (event_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    ctx->subscriber_count = 0;
    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    return 0;
}
