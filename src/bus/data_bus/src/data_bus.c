#include "data_bus.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define DATA_BUS_MAX_ITEMS_DEFAULT 32
#define DATA_BUS_MAX_ITEM_SIZE_DEFAULT (4 * 1024 * 1024)
#define DATA_BUS_MAX_SUBSCRIBERS_DEFAULT 16

// ==========================================================================
// 内部数据项结构体
// ==========================================================================
typedef struct {
    data_bus_item_info_t info;
    void *data_ptr;
    bool in_use;
    bool published;
    pthread_mutex_t ref_lock;
} data_item_t;

// ==========================================================================
// 内部订阅者结构体（推模式核心）
// ==========================================================================
typedef struct {
    data_type_t type;
    data_bus_callback_t cb;
    void *user_data;
    bool valid;
} data_subscriber_t;

// ==========================================================================
// 内部上下文结构体
// ==========================================================================
typedef struct {
    data_bus_config_t config;
    data_item_t *items;
    data_subscriber_t *subscribers; // 订阅者数组
    uint32_t max_items;
    uint32_t max_subscribers;
    size_t max_item_size;
    int latest_item_index;
    data_item_t *latest_item_held;
    void *memory_pool;
    pthread_mutex_t lock;
    pthread_rwlock_t rwlock;
} data_bus_context_t;

// ==========================================================================
// 字符串映射表
// ==========================================================================
static const char* g_data_type_str[] = {
    [DATA_TYPE_INVALID] = "INVALID",
    [DATA_TYPE_VIDEO_FRAME] = "VIDEO_FRAME",
    [DATA_TYPE_AI_RESULT] = "AI_RESULT",
    [DATA_TYPE_AUDIO_FRAME] = "AUDIO_FRAME",
};

// ==========================================================================
// 内部辅助函数
// ==========================================================================
static uint64_t _data_bus_get_timestamp_us(void);
static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx);
static void _data_bus_reset_item(data_item_t *item);
static void _data_bus_notify_subscribers(data_bus_context_t *ctx, data_item_t *item);

// ==========================================================================
// 对外API实现
// ==========================================================================
int data_bus_init(const data_bus_config_t *config, data_bus_handle_t *out_handle) {
    if (!config || !out_handle) return -1;

    data_bus_context_t *ctx = calloc(1, sizeof(data_bus_context_t));
    if (!ctx) return -1;

    ctx->config = *config;
    ctx->max_items = config->max_items ? config->max_items : DATA_BUS_MAX_ITEMS_DEFAULT;
    ctx->max_item_size = config->max_item_size ? config->max_item_size : DATA_BUS_MAX_ITEM_SIZE_DEFAULT;
    ctx->max_subscribers = config->max_subscribers ? config->max_subscribers : DATA_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->latest_item_index = -1;
    ctx->latest_item_held = NULL;

    // 分配数据项
    ctx->items = calloc(ctx->max_items, sizeof(data_item_t));
    ctx->subscribers = calloc(ctx->max_subscribers, sizeof(data_subscriber_t));
    ctx->memory_pool = malloc(ctx->max_items * ctx->max_item_size);
    if (!ctx->items || !ctx->memory_pool || !ctx->subscribers) {
        free(ctx->subscribers); free(ctx->items); free(ctx->memory_pool); free(ctx);
        return -1;
    }

    // 初始化item
    for (uint32_t i=0; i<ctx->max_items; i++) {
        ctx->items[i].data_ptr = (uint8_t*)ctx->memory_pool + i * ctx->max_item_size;
        pthread_mutex_init(&ctx->items[i].ref_lock, NULL);
    }

    pthread_mutex_init(&ctx->lock, NULL);
    pthread_rwlock_init(&ctx->rwlock, NULL);
    *out_handle = ctx;

    LOG_I("Data Bus: Init OK (items=%u, subs=%u)", ctx->max_items, ctx->max_subscribers);
    return 0;
}

// 生产者：分配
int data_bus_alloc(data_bus_handle_t handle, data_type_t type, size_t size,
                   const char *producer, data_bus_item_handle_t *out_item) {
    if (!handle || !out_item || type == DATA_TYPE_INVALID) return -1;
    data_bus_context_t *ctx = handle;
    if (size > ctx->max_item_size) return -1;

    pthread_mutex_lock(&ctx->lock);
    data_item_t *item = _data_bus_find_free_item(ctx);
    if (!item) { pthread_mutex_unlock(&ctx->lock); return -1; }

    _data_bus_reset_item(item);
    item->info.type = type;
    item->info.data_size = size;
    item->info.timestamp = _data_bus_get_timestamp_us();
    item->info.producer = producer;
    item->info.ref_count = 1;
    item->in_use = true;
    item->published = false;

    pthread_mutex_unlock(&ctx->lock);
    *out_item = item;
    return 0;
}

// 生产者：获取可写指针
void* data_bus_get_writable_ptr(data_bus_item_handle_t item) {
    data_item_t *ditem = item;
    if (!ditem || ditem->published) return NULL;
    return ditem->data_ptr;
}

// 生产者：发布 + 自动通知订阅者（推模式核心）
int data_bus_publish(data_bus_handle_t handle, data_bus_item_handle_t item) {
    if (!handle || !item) return -1;
    data_bus_context_t *ctx = handle;
    data_item_t *ditem = item;

    pthread_mutex_lock(&ctx->lock);
    if (!ditem->in_use || ditem->published) { pthread_mutex_unlock(&ctx->lock); return -1; }

    // 释放旧数据
    if (ctx->latest_item_held) {
        pthread_mutex_lock(&ctx->latest_item_held->ref_lock);
        ctx->latest_item_held->info.ref_count--;
        if (ctx->latest_item_held->info.ref_count == 0) _data_bus_reset_item(ctx->latest_item_held);
        pthread_mutex_unlock(&ctx->latest_item_held->ref_lock);
    }

    ditem->published = true;
    ctx->latest_item_index = (ditem - ctx->items);
    pthread_mutex_lock(&ditem->ref_lock);
    ditem->info.ref_count++;
    pthread_mutex_unlock(&ditem->ref_lock);
    ctx->latest_item_held = ditem;

    // 【关键】推模式：通知所有订阅者
    _data_bus_notify_subscribers(ctx, ditem);
    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

// 消费者：订阅（人脸服务必需）
int data_bus_subscribe(data_bus_handle_t handle, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_t *out_sub) {
    if (!handle || !cb || !out_sub) return -1;
    data_bus_context_t *ctx = handle;

    pthread_mutex_lock(&ctx->lock);
    for (uint32_t i=0; i<ctx->max_subscribers; i++) {
        if (!ctx->subscribers[i].valid) {
            ctx->subscribers[i].type = type;
            ctx->subscribers[i].cb = cb;
            ctx->subscribers[i].user_data = user_data;
            ctx->subscribers[i].valid = true;
            *out_sub = &ctx->subscribers[i];
            pthread_mutex_unlock(&ctx->lock);
            LOG_I("Data Bus: Subscribe OK (type=0x%x)", type);
            return 0;
        }
    }
    pthread_mutex_unlock(&ctx->lock);
    LOG_E("Data Bus: Subscribe full");
    return -1;
}

// 消费者：取消订阅
int data_bus_unsubscribe(data_bus_handle_t handle, data_bus_subscription_t *sub) {
    if (!handle || !sub || !*sub) return -1;
    data_bus_context_t *ctx = handle;
    data_subscriber_t *s = *sub;

    pthread_mutex_lock(&ctx->lock);
    s->valid = false;
    pthread_mutex_unlock(&ctx->lock);
    *sub = NULL;
    LOG_I("Data Bus: Unsubscribe OK");
    return 0;
}

// 消费者：拉模式 - 获取最新
int data_bus_acquire_latest(data_bus_handle_t handle, data_type_t type,
                             data_bus_item_handle_t *out_item) {
    if (!handle || !out_item) return -1;
    data_bus_context_t *ctx = handle;
    pthread_rwlock_rdlock(&ctx->rwlock);

    if (ctx->latest_item_index < 0) { pthread_rwlock_unlock(&ctx->rwlock); return -1; }
    data_item_t *item = &ctx->items[ctx->latest_item_index];
    if (type != DATA_TYPE_INVALID && item->info.type != type) { pthread_rwlock_unlock(&ctx->rwlock); return -1; }

    pthread_mutex_lock(&item->ref_lock);
    item->info.ref_count++;
    pthread_mutex_unlock(&item->ref_lock);
    pthread_rwlock_unlock(&ctx->rwlock);
    *out_item = item;
    return 0;
}

const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item) {
    return item ? ((data_item_t*)item)->data_ptr : NULL;
}

int data_bus_get_item_info(data_bus_item_handle_t item, data_bus_item_info_t *out_info) {
    if (!item || !out_info) return -1;
    data_item_t *ditem = item;
    pthread_mutex_lock(&ditem->ref_lock);
    *out_info = ditem->info;
    pthread_mutex_unlock(&ditem->ref_lock);
    return 0;
}

int data_bus_release(data_bus_item_handle_t item) {
    if (!item) return -1;
    data_item_t *ditem = item;

    pthread_mutex_lock(&ditem->ref_lock);
    if (ditem->info.ref_count == 0) { pthread_mutex_unlock(&ditem->ref_lock); return -1; }
    ditem->info.ref_count--;
    uint32_t cnt = ditem->info.ref_count;
    pthread_mutex_unlock(&ditem->ref_lock);

    if (cnt == 0) _data_bus_reset_item(ditem);
    return 0;
}

int data_bus_deinit(data_bus_handle_t handle) {
    if (!handle) return -1;
    data_bus_context_t *ctx = handle;

    pthread_rwlock_wrlock(&ctx->rwlock);
    if (ctx->latest_item_held) _data_bus_reset_item(ctx->latest_item_held);
    for (uint32_t i=0; i<ctx->max_items; i++) pthread_mutex_destroy(&ctx->items[i].ref_lock);

    free(ctx->memory_pool);
    free(ctx->items);
    free(ctx->subscribers);
    pthread_rwlock_unlock(&ctx->rwlock);

    pthread_rwlock_destroy(&ctx->rwlock);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    LOG_I("Data Bus: Deinit OK");
    return 0;
}

const char* data_type_to_str(data_type_t type) {
    return (type < DATA_TYPE_MAX) ? g_data_type_str[type] : "UNKNOWN";
}

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================
static uint64_t _data_bus_get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx) {
    for (uint32_t i=0; i<ctx->max_items; i++) if (!ctx->items[i].in_use) return &ctx->items[i];
    return NULL;
}

static void _data_bus_reset_item(data_item_t *item) {
    if (!item) return;
    memset(&item->info, 0, sizeof(item->info));
    item->in_use = false;
    item->published = false;
}

// 推模式：通知订阅者
static void _data_bus_notify_subscribers(data_bus_context_t *ctx, data_item_t *item) {
    for (uint32_t i=0; i<ctx->max_subscribers; i++) {
        data_subscriber_t *s = &ctx->subscribers[i];
        if (s->valid && (s->type == DATA_TYPE_INVALID || s->type == item->info.type)) {
            // 引用+1，保证回调中数据安全
            pthread_mutex_lock(&item->ref_lock);
            item->info.ref_count++;
            pthread_mutex_unlock(&item->ref_lock);
            s->cb((data_bus_item_handle_t)item, s->user_data);
            data_bus_release((data_bus_item_handle_t)item);
        }
    }
}