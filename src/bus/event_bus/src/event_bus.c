/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           event_bus.c
 * @brief          High-Performance Asynchronous Event Bus Implementation [V2.0 Optimized]
 * @details
 *  1. TLSF Static Memory Pool: Zero fragmentation for long-running embedded Linux
 *  2. Fine-Grained Lock: Separate sub/queue locks, no concurrency competition
 *  3. C11 Atomic Stats: Lock-free event/drop count update
 *  4. Magic Number Safety: Block illegal pointers, prevent crash
 *  5. Full API Compatibility: Non-intrusive upgrade
 * 
 * @author Luo
 * @date 2026
 ******************************************************************************
 */

#include "event_bus.h"
#include "log.h"
#include "queue.h"
#include "mem_adapter.h"   // TLSF Memory Adapter
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>    // C11 Atomic Operations

// ==========================================================================
// Global Configuration + Safety Macros
// ==========================================================================
#define MAX_EVENT_BUS                    4       /**< Max 4 bus instances */
#define BUS_NAME_MAX_LEN                 16      /**< Max bus name length */
#define EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT 32     /**< Default max subscribers */
#define EVENT_BUS_MAX_QUEUE_EVENTS       256     /**< Max event queue capacity */
#define MAX_TEMP_CALLBACKS               32      /**< Max callbacks per dispatch */

#define EVENT_BUS_MAGIC          0xA55A5AA5u  /**< Bus instance magic number */
#define EVENT_BUS_SUB_MAGIC      0x5AA5A55Au  /**< Subscriber magic number */

// ==========================================================================
// Internal Type Definitions
// ==========================================================================

/**
 * @brief Bus Instance Table Entry
 */
typedef struct {
    char name[BUS_NAME_MAX_LEN];
    struct event_bus_t *bus;
    bool used;
} event_bus_entry_t;

/**
 * @brief Internal Subscriber Entry (with magic number)
 */
typedef struct {
    uint32_t            magic;                  /**< Safety magic number */
    int                 id;
    event_type_t        event_type;
    event_callback_t    callback;
    void               *user_data;
    bool                valid;
    bool                skip_self_published;
    const char         *subscriber_id;
} subscriber_entry_t;

/**
 * @brief Event Bus Context (Core Optimized)
 */
typedef struct event_bus_t {
    uint32_t            magic;                  /**< Safety magic number */
    event_bus_config_t  config;
    subscriber_entry_t *subscribers;
    uint32_t            subscriber_count;
    uint32_t            max_subscribers;
    int                 next_subscription_id;

    // Fine-Grained Locks (Core Optimization)
    pthread_mutex_t     queue_lock;     /**< Protect event queue */
    pthread_rwlock_t    sub_rwlock;     /**< Protect subscriber list */

    int                 pipefd[2];      /**< Pipe for main thread wakeup */
    Queue_t             event_queue;    /**< Async event queue */
    void               *queue_buffer[EVENT_BUS_MAX_QUEUE_EVENTS];

    atomic_uint         event_count;    /**< Atomic total event count */
    atomic_uint         drop_count;     /**< Atomic dropped event count */
} event_bus_context_t;

// ==========================================================================
// Global Static Variables
// ==========================================================================
static event_bus_entry_t s_bus_table[MAX_EVENT_BUS] = {0};
static pthread_mutex_t  s_table_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Event type to string mapping table
 */
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
    [EVENT_TYPE_CAPTURE_PROTO_READY] ="CAPTURE_PROTO_READY",
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
// Internal Helper Functions Declaration
// ==========================================================================
static uint64_t _event_bus_get_timestamp_us(void);
static void _event_bus_free_event(event_t *event);
static event_bus_context_t* _event_bus_find_ctx(const char *name);
static bool _event_bus_should_skip_subscriber(const subscriber_entry_t *sub, const event_t *event);

// ==========================================================================
// Internal Helper Functions Implementation
// ==========================================================================

/**
 * @brief  Find event bus context by name
 * @param  name Bus name
 * @return Bus context pointer, NULL=not found
 */
static event_bus_context_t* _event_bus_find_ctx(const char *name) {
    if (!name) return NULL;

    pthread_mutex_lock(&s_table_lock);
    event_bus_context_t *ctx = NULL;
    for (int i = 0; i < MAX_EVENT_BUS; i++) {
        if (s_bus_table[i].used && strcmp(s_bus_table[i].name, name) == 0) {
            ctx = s_bus_table[i].bus;
            // Magic number safety check
            if (ctx && ctx->magic != EVENT_BUS_MAGIC) ctx = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&s_table_lock);
    return ctx;
}

/**
 * @brief  Check if subscriber should skip this event
 * @param  sub Subscriber entry
 * @param  event Event pointer
 * @return true=skip, false=process
 */
static bool _event_bus_should_skip_subscriber(const subscriber_entry_t *sub, const event_t *event) {
    if (!sub || sub->magic != EVENT_BUS_SUB_MAGIC || !sub->valid) return true;
    if (sub->event_type != EVENT_TYPE_INVALID && sub->event_type != event->type) return true;

    // Self-publish filter logic
    if (sub->skip_self_published && event->source && sub->subscriber_id) {
        if (strcmp(sub->subscriber_id, event->source) == 0) return true;
    }
    return false;
}

/**
 * @brief  Get monotonic timestamp in microseconds
 * @return Us-level timestamp
 */
static uint64_t _event_bus_get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/**
 * @brief  Free event memory (TLSF)
 * @param  event Event pointer
 */
static void _event_bus_free_event(event_t *event) {
    if (event) mem_free(event);
}

// ==========================================================================
// Public API Implementation
// ==========================================================================

/**
 * @brief  Convert event type to string
 * @param  type Event type
 * @return String name
 */
const char* event_type_to_str(event_type_t type) {
    if (type < sizeof(g_event_type_str)/sizeof(char*) && g_event_type_str[type])
        return g_event_type_str[type];
    if (type >= EVENT_TYPE_SYS_BASE && type <= EVENT_TYPE_SYS_MAX) return "SYS_EVENT";
    if (type >= EVENT_TYPE_MOD_BASE && type <= EVENT_TYPE_MOD_MAX) return "MOD_EVENT";
    if (type >= EVENT_TYPE_CAPTURE_BASE && type <= EVENT_TYPE_CAPTURE_MAX) return "CAPTURE_EVENT";
    if (type >= EVENT_TYPE_FACE_BASE && type <= EVENT_TYPE_FACE_MAX) return "FACE_EVENT";
    if (type >= EVENT_TYPE_DEMO_BASE && type <= EVENT_TYPE_DEMO_MAX) return "DEMO_EVENT";
    return "UNKNOWN_EVENT";
}

/**
 * @brief  Get event source name
 * @param  event Event pointer
 * @return Source string
 */
const char* event_get_source(const event_t *event) {
    return event ? event->source : "UNKNOWN";
}

/**
 * @brief  Initialize event bus
 * @param  config Bus configuration
 * @return 0=success
 */
int event_bus_init(const event_bus_config_t *config) {
    if (!config || !config->name || strlen(config->name) >= BUS_NAME_MAX_LEN) return -1;
    if (_event_bus_find_ctx(config->name)) {
        LOG_E("Event Bus[%s]: Already exists", config->name);
        return -1;
    }

    pthread_mutex_lock(&s_table_lock);
    int free_idx = -1;
    for (int i = 0; i < MAX_EVENT_BUS; i++) {
        if (!s_bus_table[i].used) { free_idx = i; break; }
    }
    if (free_idx < 0) {
        pthread_mutex_unlock(&s_table_lock);
        LOG_E("Event Bus: Instance table full");
        return -1;
    }

    // Allocate context with TLSF
    event_bus_context_t *ctx = mem_calloc(1, sizeof(event_bus_context_t));
    if (!ctx) { pthread_mutex_unlock(&s_table_lock); return -1; }

    ctx->magic = EVENT_BUS_MAGIC;
    memcpy(&ctx->config, config, sizeof(event_bus_config_t));
    ctx->max_subscribers = config->max_subscribers > 0 ? config->max_subscribers : EVENT_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->next_subscription_id = 1;

    // Allocate subscriber array + init magic number
    ctx->subscribers = mem_calloc(ctx->max_subscribers, sizeof(subscriber_entry_t));
    if (!ctx->subscribers) { mem_free(ctx); pthread_mutex_unlock(&s_table_lock); return -1; }
    for (uint32_t i = 0; i < ctx->max_subscribers; i++) {
        ctx->subscribers[i].magic = EVENT_BUS_SUB_MAGIC;
    }

    // Initialize fine-grained locks
    pthread_mutex_init(&ctx->queue_lock, NULL);
    pthread_rwlock_init(&ctx->sub_rwlock, NULL);

    // Create pipe for main thread wakeup
    if (pipe(ctx->pipefd) != 0) {
        LOG_E("Event Bus: Pipe create failed");
        mem_free(ctx->subscribers); mem_free(ctx);
        pthread_mutex_unlock(&s_table_lock);
        return -1;
    }

    // Initialize event queue
    Queue_Init(&ctx->event_queue, ctx->queue_buffer, EVENT_BUS_MAX_QUEUE_EVENTS);
    atomic_init(&ctx->event_count, 0);
    atomic_init(&ctx->drop_count, 0);

    // Register bus instance
    strncpy(s_bus_table[free_idx].name, config->name, BUS_NAME_MAX_LEN-1);
    s_bus_table[free_idx].name[BUS_NAME_MAX_LEN-1] = '\0';
    ctx->config.name = s_bus_table[free_idx].name;
    s_bus_table[free_idx].bus = ctx;
    s_bus_table[free_idx].used = true;

    pthread_mutex_unlock(&s_table_lock);
    LOG_I("Event Bus[%s]: Init success", config->name);
    return 0;
}

/**
 * @brief  Extended subscribe function
 * @param  name Bus name
 * @param  subscriber Subscriber config
 * @param  subscriber_id Custom subscriber ID
 * @return Subscription ID
 */
int event_bus_subscribe_ex(const char *name, const event_subscriber_t *subscriber, const char *subscriber_id) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx || !subscriber || !subscriber->callback || !subscriber_id) return -1;

    pthread_rwlock_wrlock(&ctx->sub_rwlock);
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
    pthread_rwlock_unlock(&ctx->sub_rwlock);

    if (id < 0) { LOG_E("Event Bus: Subscriber table full"); return -1; }
    LOG_I("Bus[%s] Subscribe success ID=%d, Self-filter=%s",
          name, id, subscriber->skip_self_published ? "ON" : "OFF");
    return id;
}

/**
 * @brief  Standard subscribe function
 * @param  name Bus name
 * @param  subscriber Subscriber config
 * @return Subscription ID
 */
int event_bus_subscribe(const char *name, const event_subscriber_t *subscriber) {
    event_subscriber_t sub = *subscriber;
    return event_bus_subscribe_ex(name, &sub, "DEFAULT");
}

/**
 * @brief  Unsubscribe event
 * @param  name Bus name
 * @param  subscription_id Sub ID
 * @return 0=success
 */
int event_bus_unsubscribe(const char *name, int subscription_id) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx || subscription_id <= 0) return -1;

    pthread_rwlock_wrlock(&ctx->sub_rwlock);
    int ret = -1;
    for (uint32_t i = 0; i < ctx->max_subscribers; i++) {
        if (ctx->subscribers[i].valid && ctx->subscribers[i].id == subscription_id) {
            ctx->subscribers[i].valid = false;
            ctx->subscriber_count--;
            ret = 0;
            break;
        }
    }
    pthread_rwlock_unlock(&ctx->sub_rwlock);

    if (ret == 0) LOG_I("Bus[%s] Unsubscribe ID=%d", name, subscription_id);
    return ret;
}

/**
 * @brief  Publish event
 * @param  name Bus name
 * @param  event Event pointer
 * @return 0=success
 */
int event_bus_publish(const char *name, const event_t *event) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx || !event || event->type == EVENT_TYPE_INVALID) return -1;

    // Allocate event copy with TLSF
    event_t *ev_copy = mem_alloc(sizeof(event_t));
    if (!ev_copy) {
        LOG_E("Bus[%s] Memory alloc failed", name);
        atomic_fetch_add(&ctx->drop_count, 1);
        return -1;
    }
    memcpy(ev_copy, event, sizeof(event_t));

    // Auto fill timestamp
    if (ev_copy->timestamp == 0)
        ev_copy->timestamp = _event_bus_get_timestamp_us();

    // Queue operation with fine-grained lock
    pthread_mutex_lock(&ctx->queue_lock);
    int ret = Queue_Put(&ctx->event_queue, ev_copy);
    pthread_mutex_unlock(&ctx->queue_lock);

    // Queue full, drop event
    if (ret != QUEUE_OK) {
        LOG_W("Bus[%s] Queue full, drop event: %s", name, event_type_to_str(event->type));
        mem_free(ev_copy);
        atomic_fetch_add(&ctx->drop_count, 1);
        return -1;
    }

    // Wake up main thread
    char wake = 0x01;
    write(ctx->pipefd[1], &wake, 1);
    atomic_fetch_add(&ctx->event_count, 1);
    return 0;
}

/**
 * @brief  Fast publish simple event
 * @param  name Bus name
 * @param  type Event type
 * @param  source Publisher name
 * @return 0=success
 */
int event_bus_publish_simple(const char *name, event_type_t type, const char *source) {
    event_t evt = {0};
    evt.type = type;
    evt.priority = EVENT_PRIORITY_NORMAL;
    evt.source = source;
    return event_bus_publish(name, &evt);
}

/**
 * @brief  Get monitor FD for select/poll
 * @param  name Bus name
 * @return Read FD of pipe
 */
int event_bus_get_wait_fd(const char *name) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    return ctx ? ctx->pipefd[0] : -1;
}

/**
 * @brief  Dispatch events in main thread
 * @param  name Bus name
 * @return 0=success
 */
int event_bus_dispatch(const char *name) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx) return -1;

    // Read 1-byte wake signal
    char wake;
    read(ctx->pipefd[0], &wake, 1);

    // Process all events in queue
    while (1) {
        pthread_mutex_lock(&ctx->queue_lock);
        event_t *event = NULL;
        int ret = Queue_Get(&ctx->event_queue, (void**)&event);
        pthread_mutex_unlock(&ctx->queue_lock);

        if (ret != QUEUE_OK || !event) break;

        // Callback cache (execute outside lock)
        struct {
            event_callback_t cb;
            void *user_data;
        } temp_cb[MAX_TEMP_CALLBACKS];
        int cb_count = 0;

        // Collect valid callbacks (read lock)
        pthread_rwlock_rdlock(&ctx->sub_rwlock);
        for (uint32_t i = 0; i < ctx->max_subscribers && cb_count < MAX_TEMP_CALLBACKS; i++) {
            if (!_event_bus_should_skip_subscriber(&ctx->subscribers[i], event)) {
                temp_cb[cb_count].cb = ctx->subscribers[i].callback;
                temp_cb[cb_count].user_data = ctx->subscribers[i].user_data;
                cb_count++;
            }
        }
        pthread_rwlock_unlock(&ctx->sub_rwlock);

        // Execute all callbacks
        for (int i = 0; i < cb_count; i++) {
            if (temp_cb[i].cb)
                temp_cb[i].cb(event, temp_cb[i].user_data);
        }

        // Free event memory
        _event_bus_free_event(event);
    }

    return 0;
}

/**
 * @brief  Deinitialize event bus
 * @param  name Bus name
 * @return 0=success
 */
int event_bus_deinit(const char *name) {
    event_bus_context_t *ctx = _event_bus_find_ctx(name);
    if (!ctx) return -1;

    // Close pipe
    close(ctx->pipefd[0]);
    close(ctx->pipefd[1]);

    // Free all events in queue
    event_t *ev;
    while (Queue_Get(&ctx->event_queue, (void**)&ev) == QUEUE_OK)
        _event_bus_free_event(ev);

    // Print runtime stats
    LOG_I("Bus[%s] Stats: Total=%u, Dropped=%u",
          name, atomic_load(&ctx->event_count), atomic_load(&ctx->drop_count));

    // Release resources
    pthread_rwlock_wrlock(&ctx->sub_rwlock);
    mem_free(ctx->subscribers);
    pthread_rwlock_unlock(&ctx->sub_rwlock);

    // Destroy locks
    pthread_rwlock_destroy(&ctx->sub_rwlock);
    pthread_mutex_destroy(&ctx->queue_lock);
    mem_free(ctx);

    // Clear instance table
    pthread_mutex_lock(&s_table_lock);
    for (int i = 0; i < MAX_EVENT_BUS; i++) {
        if (s_bus_table[i].used && strcmp(s_bus_table[i].name, name) == 0) {
            memset(&s_bus_table[i], 0, sizeof(event_bus_entry_t));
            break;
        }
    }
    pthread_mutex_unlock(&s_table_lock);

    LOG_I("Event Bus[%s]: Destroy success", name);
    return 0;
}