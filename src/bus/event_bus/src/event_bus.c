/* SPDX-License-Identifier: MIT */
/**
 * @file event_bus.c
 * @brief 嵌入式Linux 异步事件总线实现
 * @details 1. 发布者自订阅灵活可配置 2. 线程安全 3. 异步队列 4. 主线程分发
 *          彻底解决死循环、内存泄漏、并发冲突问题
 * @author Luo
 * @date 2025
 */

#include "event_bus.h"
#include "log.h"
#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

// ==========================================================================
// 全局配置
// ==========================================================================
#define MAX_EVENT_BUS                    4       /**< 最大支持4个总线实例 */
#define BUS_NAME_MAX_LEN                 16      /**< 总线名称最大长度 */
#define EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT 32     /**< 默认最大订阅者数 */
#define EVENT_BUS_MAX_QUEUE_EVENTS       256     /**< 事件队列最大容量 */
#define MAX_TEMP_CALLBACKS               32      /**< 单次分发最大回调数 */

// ==========================================================================
// 内部类型定义
// ==========================================================================

/**
 * @brief 总线实例表条目
 */
typedef struct {
    char name[BUS_NAME_MAX_LEN];
    struct event_bus_t *bus;
    bool used;
} event_bus_entry_t;

/**
 * @brief 内部订阅者条目
 */
typedef struct {
    int                 id;                     /**< 订阅唯一ID */
    event_type_t        event_type;            /**< 订阅事件类型 */
    event_callback_t    callback;              /**< 回调函数 */
    void               *user_data;             /**< 用户数据 */
    bool                valid;                 /**< 有效标记 */
    bool                skip_self_published;   /**< 自发布过滤开关 */
    const char         *subscriber_id;         /**< 订阅者标识 */
} subscriber_entry_t;

/**
 * @brief 事件总线上下文
 */
typedef struct event_bus_t {
    event_bus_config_t  config;                /**< 总线配置 */
    subscriber_entry_t *subscribers;           /**< 订阅者数组 */
    uint32_t            subscriber_count;       /**< 当前订阅数 */
    uint32_t            max_subscribers;        /**< 最大订阅数 */
    int                 next_subscription_id;  /**< 下一个订阅ID */

    pthread_mutex_t     lock;                  /**< 互斥锁 */
    pthread_rwlock_t    rwlock;                /**< 读写锁（遍历用） */

    int                 pipefd[2];             /**< 异步唤醒管道 */
    Queue_t             event_queue;           /**< 事件队列 */
    void               *queue_buffer[EVENT_BUS_MAX_QUEUE_EVENTS];

    uint32_t            event_count;           /**< 总事件计数 */
    uint32_t            drop_count;            /**< 丢包计数 */
} event_bus_context_t;

// ==========================================================================
// 全局静态变量
// ==========================================================================
static event_bus_entry_t s_bus_table[MAX_EVENT_BUS] = {0};
static pthread_mutex_t s_table_lock = PTHREAD_MUTEX_INITIALIZER;

// 事件类型字符串映射表
static const char* g_event_type_str[] = {
    [EVENT_TYPE_INVALID]            = "INVALID",
    [EVENT_TYPE_SYS_CORE_READY]     = "SYS_CORE_READY",
    [EVENT_TYPE_SYS_PAUSE]          = "SYS_PAUSE",
    [EVENT_TYPE_SYS_RESUME]         = "SYS_RESUME",
    [EVENT_TYPE_SYS_STOP]           = "SYS_STOP",
    [EVENT_TYPE_SYS_SHUTDOWN]       = "SYS_SHUTDOWN",
    [EVENT_TYPE_SYS_ERROR]          = "SYS_ERROR",

    [EVENT_TYPE_MOD_INIT_DONE]      = "MOD_INIT_DONE",
    [EVENT_TYPE_MOD_START_DONE]     = "MOD_START_DONE",
    [EVENT_TYPE_MOD_PAUSED]         = "MOD_PAUSED",
    [EVENT_TYPE_MOD_RESUMED]        = "MOD_RESUMED",
    [EVENT_TYPE_MOD_STOPPED]        = "MOD_STOPPED",
    [EVENT_TYPE_MOD_FAULT]          = "MOD_FAULT",

    [EVENT_TYPE_CAPTURE_READY]      = "CAPTURE_READY",
    [EVENT_TYPE_CAPTURE_RUNNING]    = "CAPTURE_RUNNING",
    [EVENT_TYPE_CAPTURE_FRAME_READY] = "CAPTURE_FRAME_READY",
    [EVENT_TYPE_CAPTURE_STOPPED]    = "CAPTURE_STOPPED",
    [EVENT_TYPE_CAPTURE_ERROR]      = "CAPTURE_ERROR",

    [EVENT_TYPE_FACE_READY]         = "FACE_READY",
    [EVENT_TYPE_FACE_RUNNING]       = "FACE_RUNNING",
    [EVENT_TYPE_FACE_PROCESS_START] = "FACE_PROCESS_START",
    [EVENT_TYPE_FACE_PROCESS_DONE]  = "FACE_PROCESS_DONE",
    [EVENT_TYPE_FACE_STOPPED]       = "FACE_STOPPED",
    [EVENT_TYPE_FACE_ERROR]         = "FACE_ERROR",

    [EVENT_TYPE_DEMO_RUNNING]       = "DEMO_RUNNING",
    [EVENT_TYPE_DEMO_EXIT]          = "DEMO_EXIT"
};

// ==========================================================================
// 内部工具函数声明
// ==========================================================================
static uint64_t _event_bus_get_timestamp_us(void);
static void _event_bus_free_event(event_t *event);
static event_bus_context_t* _event_bus_find_ctx(const char *name);
static bool _event_bus_should_skip_subscriber(const subscriber_entry_t *sub, const event_t *event);

// ==========================================================================
// 内部函数实现
// ==========================================================================

/**
 * @brief 根据名称查找总线实例
 */
static event_bus_context_t* _event_bus_find_ctx(const char *name) {
    if (!name) return NULL;

    pthread_mutex_lock(&s_table_lock);
    event_bus_context_t *ctx = NULL;
    for (int i = 0; i < MAX_EVENT_BUS; i++) {
        if (s_bus_table[i].used && strcmp(s_bus_table[i].name, name) == 0) {
            ctx = s_bus_table[i].bus;
            break;
        }
    }
    pthread_mutex_unlock(&s_table_lock);
    return ctx;
}

/**
 * @brief 核心：判断是否跳过当前订阅者（自发布过滤逻辑）
 */
static bool _event_bus_should_skip_subscriber(const subscriber_entry_t *sub, const event_t *event) {
    if (!sub->valid) return true;
    if (sub->event_type != EVENT_TYPE_INVALID && sub->event_type != event->type) return true;

    // 发布者自订阅过滤：开启过滤 + 发布者/订阅者标识一致 → 跳过
    if (sub->skip_self_published && event->source && sub->subscriber_id) {
        if (strcmp(sub->subscriber_id, event->source) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 获取微秒时间戳
 */
static uint64_t _event_bus_get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/**
 * @brief 释放事件内存
 */
static void _event_bus_free_event(event_t *event) {
    if (event) free(event);
}

// ==========================================================================
// 对外接口实现
// ==========================================================================

const char* event_type_to_str(event_type_t type) {
    if (type < sizeof(g_event_type_str)/sizeof(char*) && g_event_type_str[type]) {
        return g_event_type_str[type];
    }
    if (type >= EVENT_TYPE_SYS_BASE && type <= EVENT_TYPE_SYS_MAX) return "SYS_EVENT";
    if (type >= EVENT_TYPE_MOD_BASE && type <= EVENT_TYPE_MOD_MAX) return "MOD_EVENT";
    if (type >= EVENT_TYPE_CAPTURE_BASE && type <= EVENT_TYPE_CAPTURE_MAX) return "CAPTURE_EVENT";
    if (type >= EVENT_TYPE_FACE_BASE && type <= EVENT_TYPE_FACE_MAX) return "FACE_EVENT";
    if (type >= EVENT_TYPE_DEMO_BASE && type <= EVENT_TYPE_DEMO_MAX) return "DEMO_EVENT";
    return "UNKNOWN_EVENT";
}

const char* event_get_source(const event_t *event) {
    return event ? event->source : "UNKNOWN";
}

int event_bus_init(const event_bus_config_t *config) {
    if (!config || !config->name || strlen(config->name) >= BUS_NAME_MAX_LEN) {
        return -1;
    }

    if (_event_bus_find_ctx(config->name)) {
        LOG_E("Event Bus[%s]: 已存在", config->name);
        return -1;
    }

    pthread_mutex_lock(&s_table_lock);
    int free_idx = -1;
    for (int i = 0; i < MAX_EVENT_BUS; i++) {
        if (!s_bus_table[i].used) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        pthread_mutex_unlock(&s_table_lock);
        LOG_E("Event Bus: 实例表已满");
        return -1;
    }

    event_bus_context_t *ctx = (event_bus_context_t*)malloc(sizeof(event_bus_context_t));
    if (!ctx) {
        pthread_mutex_unlock(&s_table_lock);
        return -1;
    }
    memset(ctx, 0, sizeof(event_bus_context_t));

    memcpy(&ctx->config, config, sizeof(event_bus_config_t));
    ctx->max_subscribers = config->max_subscribers > 0 ? config->max_subscribers : EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->next_subscription_id = 1;

    ctx->subscribers = (subscriber_entry_t*)malloc(ctx->max_subscribers * sizeof(subscriber_entry_t));
    if (!ctx->subscribers) {
        free(ctx);
        pthread_mutex_unlock(&s_table_lock);
        return -1;
    }
    memset(ctx->subscribers, 0, ctx->max_subscribers * sizeof(subscriber_entry_t));

    pthread_mutex_init(&ctx->lock, NULL);
    pthread_rwlock_init(&ctx->rwlock, NULL);

    if (pipe(ctx->pipefd) != 0) {
        LOG_E("Event Bus: 管道创建失败");
        free(ctx->subscribers);
        free(ctx);
        pthread_mutex_unlock(&s_table_lock);
        return -1;
    }

    Queue_Init(&ctx->event_queue, ctx->queue_buffer, EVENT_BUS_MAX_QUEUE_EVENTS);
    ctx->event_count = 0;
    ctx->drop_count = 0;

    strncpy(s_bus_table[free_idx].name, config->name, BUS_NAME_MAX_LEN-1);
    s_bus_table[free_idx].bus = ctx;
    s_bus_table[free_idx].used = true;

    pthread_mutex_unlock(&s_table_lock);
    LOG_I("Event Bus[%s]: 初始化成功", config->name);
    return 0;
}

int event_bus_subscribe_ex(const char *name, const event_subscriber_t *subscriber, const char *subscriber_id) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx || !subscriber || !subscriber->callback || !subscriber_id) {
        return -1;
    }

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
            ctx->subscribers[i].skip_self_published = subscriber->skip_self_published;
            ctx->subscribers[i].subscriber_id = subscriber_id;
            ctx->subscriber_count++;
            break;
        }
    }
    pthread_rwlock_unlock(&ctx->rwlock);

    if (id < 0) {
        LOG_E("Event Bus: 订阅位已满");
        return -1;
    }
    LOG_I("Bus[%s] 订阅成功 ID=%d, 自过滤=%s",
          name, id, subscriber->skip_self_published ? "开启" : "关闭");
    return id;
}

int event_bus_subscribe(const char *name, const event_subscriber_t *subscriber) {
    event_subscriber_t sub = *subscriber;
    sub.skip_self_published = true; // 默认：跳过自己发布的事件
    return event_bus_subscribe_ex(name, &sub, "DEFAULT");
}

int event_bus_unsubscribe(const char *name, int subscription_id) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx || subscription_id <= 0) return -1;

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
        LOG_I("Bus[%s] 取消订阅 ID=%d", name, subscription_id);
    }
    return ret;
}

int event_bus_publish(const char *name, const event_t *event) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx || !event || event->type == EVENT_TYPE_INVALID) return -1;

    event_t *ev_copy = (event_t*)malloc(sizeof(event_t));
    if (!ev_copy) {
        LOG_E("Bus[%s] 内存分配失败", name);
        ctx->drop_count++;
        return -1;
    }
    memcpy(ev_copy, event, sizeof(event_t));

    if (ev_copy->timestamp == 0) {
        ev_copy->timestamp = _event_bus_get_timestamp_us();
    }

    if (Queue_Put(&ctx->event_queue, ev_copy) != QUEUE_OK) {
        LOG_W("Bus[%s] 队列满，丢弃事件: %s", name, event_type_to_str(event->type));
        free(ev_copy);
        ctx->drop_count++;
        return -1;
    }

    char wake = 0x01;
    write(ctx->pipefd[1], &wake, 1);
    ctx->event_count++;
    return 0;
}

int event_bus_publish_simple(const char *name, event_type_t type, const char *source) {
    event_t evt = {0};
    evt.type = type;
    evt.priority = EVENT_PRIORITY_NORMAL;
    evt.source = source;
    return event_bus_publish(name, &evt);
}

int event_bus_get_wait_fd(const char *name) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    return ctx ? ctx->pipefd[0] : -1;
}

int event_bus_dispatch(const char *name) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx) return -1;

    // 清空管道唤醒信号
    char buf[16];
    read(ctx->pipefd[0], buf, sizeof(buf));

    // 取出事件
    event_t *event = NULL;
    if (Queue_Get(&ctx->event_queue, (void**)&event) != QUEUE_OK) return -1;
    if (!event) return -1;

    // 锁外执行回调，避免死锁
    struct {
        event_callback_t cb;
        void *user_data;
    } temp_cb[MAX_TEMP_CALLBACKS];
    int cb_count = 0;

    pthread_rwlock_rdlock(&ctx->rwlock);
    for (uint32_t i = 0; i < ctx->max_subscribers && cb_count < MAX_TEMP_CALLBACKS; i++) {
        if (!_event_bus_should_skip_subscriber(&ctx->subscribers[i], event)) {
            temp_cb[cb_count].cb = ctx->subscribers[i].callback;
            temp_cb[cb_count].user_data = ctx->subscribers[i].user_data;
            cb_count++;
        }
    }
    pthread_rwlock_unlock(&ctx->rwlock);

    // 执行回调
    for (int i = 0; i < cb_count; i++) {
        if (temp_cb[i].cb) {
            temp_cb[i].cb(event, temp_cb[i].user_data);
        }
    }

    _event_bus_free_event(event);
    return 0;
}

int event_bus_deinit(const char *name) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx) return -1;

    // 关闭管道
    close(ctx->pipefd[0]);
    close(ctx->pipefd[1]);

    // 释放队列所有事件
    event_t *ev;
    while (Queue_Get(&ctx->event_queue, (void**)&ev) == QUEUE_OK) {
        _event_bus_free_event(ev);
    }

    // 打印统计信息
    LOG_I("Bus[%s] 运行统计: 总事件=%u, 丢弃=%u", name, ctx->event_count, ctx->drop_count);

    // 释放资源
    pthread_rwlock_wrlock(&ctx->rwlock);
    free(ctx->subscribers);
    pthread_rwlock_unlock(&ctx->rwlock);

    pthread_rwlock_destroy(&ctx->rwlock);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);

    // 清空实例表
    pthread_mutex_lock(&s_table_lock);
    for (int i = 0; i < MAX_EVENT_BUS; i++) {
        if (s_bus_table[i].used && strcmp(s_bus_table[i].name, name) == 0) {
            memset(&s_bus_table[i], 0, sizeof(event_bus_entry_t));
            break;
        }
    }
    pthread_mutex_unlock(&s_table_lock);

    LOG_I("Event Bus[%s]: 销毁成功", name);
    return 0;
}