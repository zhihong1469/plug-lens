#include "data_bus.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// ==========================================================================
// 默认配置宏（用户不配置时用这个）
// ==========================================================================
#define DATA_BUS_MAX_ITEMS_DEFAULT 32            // 默认最多32条数据
#define DATA_BUS_MAX_ITEM_SIZE_DEFAULT (4*1024*1024) // 单条最大4M（足够RGB帧）
#define DATA_BUS_MAX_SUBSCRIBERS_DEFAULT 16      // 最多16个订阅者

// ==========================================================================
// 内部数据项结构体 → 【真正存数据的地方】（外部看不到）
// 句柄 data_bus_item_handle_t = 这个结构体的指针
// ==========================================================================
typedef struct {
    data_bus_item_info_t info;   // 数据身份证
    void *data_ptr;              // 【核心指针】指向真实数据内存（RGB/AI）
    bool in_use;                 // 标记：是否被占用
    bool published;              // 标记：是否已发布
    pthread_mutex_t ref_lock;    // 引用计数锁（多线程安全）
} data_item_t;

// ==========================================================================
// 内部订阅者结构体 → 存储一个订阅者的信息
// ==========================================================================
typedef struct {
    data_type_t type;            // 订阅的数据类型
    data_bus_callback_t cb;      // 回调函数
    void *user_data;             // 用户参数
    bool valid;                  // 标记：是否是有效订阅
} data_subscriber_t;

// ==========================================================================
// 总线上下文 → 【总线总控结构体】
// 句柄 data_bus_handle_t = 这个结构体的指针
// ==========================================================================
typedef struct {
    data_bus_config_t config;    // 配置
    data_item_t *items;          // 数据项数组（内存池）
    data_subscriber_t *subscribers; // 订阅者数组
    uint32_t max_items;          // 最大数据项数
    uint32_t max_subscribers;    // 最大订阅者数
    size_t max_item_size;        // 单数据最大大小

    data_item_t *latest_item_held;// 最新数据的指针

    // 【内存池核心】一大块连续内存，拆分给所有数据项用
    void *memory_pool;

    pthread_mutex_t lock;        // 总线大锁（保护整个总线）
    pthread_rwlock_t rwlock;     // 读写锁（拉模式用）
} data_bus_context_t;

// ==========================================================================
// 数据类型转字符串（日志打印）
// ==========================================================================
static const char* g_data_type_str[] = {
    [DATA_TYPE_INVALID] = "INVALID",
    [DATA_TYPE_VIDEO_FRAME] = "VIDEO_FRAME",
    [DATA_TYPE_AI_RESULT] = "AI_RESULT",
    [DATA_TYPE_AUDIO_FRAME] = "AUDIO_FRAME",
};

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static uint64_t _data_bus_get_timestamp_us(void);       // 获取微秒时间戳
static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx); // 找空闲数据项
static void _data_bus_reset_item(data_item_t *item);   // 重置数据项
static void _data_bus_notify_subscribers(data_bus_context_t *ctx, data_item_t *item); // 通知订阅者

// ==========================================================================
// 对外API实现
// ==========================================================================

/**
 * @brief 初始化总线 → 创建内存池、数组、锁
 * 绕点说明：
 * 1. memory_pool 是一大块内存
 * 2. 每个 data_item 的 data_ptr 指向 pool 里的一小块
 */
int data_bus_init(const data_bus_config_t *config, data_bus_handle_t *out_handle) {
    // 参数校验
    if (!config || !out_handle) return -1;

    // 申请总线上下文内存，自动清零（calloc）
    data_bus_context_t *ctx = calloc(1, sizeof(data_bus_context_t));
    if (!ctx) return -1;

    // 加载配置，无配置则用默认值
    ctx->config = *config;
    ctx->max_items = config->max_items ? config->max_items : DATA_BUS_MAX_ITEMS_DEFAULT;
    ctx->max_item_size = config->max_item_size ? config->max_item_size : DATA_BUS_MAX_ITEM_SIZE_DEFAULT;
    ctx->max_subscribers = config->max_subscribers ? config->max_subscribers : DATA_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->latest_item_held = NULL;

    // 分配3个数组：数据项、订阅者、内存池
    ctx->items = calloc(ctx->max_items, sizeof(data_item_t));
    ctx->subscribers = calloc(ctx->max_subscribers, sizeof(data_subscriber_t));
    ctx->memory_pool = malloc(ctx->max_items * ctx->max_item_size);

    // 内存分配失败，回滚释放
    if (!ctx->items || !ctx->memory_pool || !ctx->subscribers) {
        free(ctx->subscribers); free(ctx->items); free(ctx->memory_pool); free(ctx);
        return -1;
    }

    // 【关键】把大内存池拆分给每个数据项
    // 第i个数据项 → 指向 pool + i*单条大小
    for (uint32_t i=0; i<ctx->max_items; i++) {
        ctx->items[i].data_ptr = (uint8_t*)ctx->memory_pool + i * ctx->max_item_size;
        pthread_mutex_init(&ctx->items[i].ref_lock, NULL);
    }

    // 初始化总线锁
    pthread_mutex_init(&ctx->lock, NULL);
    pthread_rwlock_init(&ctx->rwlock, NULL);

    // 输出总线句柄（外部用这个操作总线）
    *out_handle = ctx;
    LOG_I("Data Bus: 初始化成功");
    return 0;
}

/**
 * @brief 生产者申请数据项
 */
int data_bus_alloc(data_bus_handle_t handle, data_type_t type, size_t size,
                   const char *producer, data_bus_item_handle_t *out_item) {
    if (!handle || !out_item || type == DATA_TYPE_INVALID) return -1;
    // 把句柄强转为内部上下文指针（绕点，理解为：句柄=指针）
    data_bus_context_t *ctx = handle;
    if (size > ctx->max_item_size) return -1;

    // 加锁：多线程申请不冲突
    pthread_mutex_lock(&ctx->lock);
    // 找一个空闲的数据项
    data_item_t *item = _data_bus_find_free_item(ctx);
    if (!item) { pthread_mutex_unlock(&ctx->lock); return -1; }

    // 初始化数据项
    _data_bus_reset_item(item);
    item->info.type = type;
    item->info.data_size = size;
    item->info.timestamp = _data_bus_get_timestamp_us();
    item->info.producer = producer;
    item->info.ref_count = 1;  // 引用计数=1（生产者占用）
    item->in_use = true;
    item->published = false;

    pthread_mutex_unlock(&ctx->lock);
    // 输出数据项句柄（给生产者用）
    *out_item = item;
    return 0;
}

/**
 * @brief 获取可写指针 → 生产者写数据用
 */
void* data_bus_get_writable_ptr(data_bus_item_handle_t item) {
    data_item_t *ditem = item;
    // 已发布的数据不能修改（安全）
    if (!ditem || ditem->published) return NULL;
    return ditem->data_ptr;
}

/**
 * @brief 发布数据 → 通知所有订阅者（核心）
 */
int data_bus_publish(data_bus_handle_t handle, data_bus_item_handle_t item) {
    if (!handle || !item) return -1;
    data_bus_context_t *ctx = handle;
    data_item_t *ditem = item;

    pthread_mutex_lock(&ctx->lock);
    if (!ditem->in_use || ditem->published) { pthread_mutex_unlock(&ctx->lock); return -1; }

    // 释放旧的最新数据
    if (ctx->latest_item_held) {
        pthread_mutex_lock(&ctx->latest_item_held->ref_lock);
        ctx->latest_item_held->info.ref_count--;
        if (ctx->latest_item_held->info.ref_count == 0)
            _data_bus_reset_item(ctx->latest_item_held);
        pthread_mutex_unlock(&ctx->latest_item_held->ref_lock);
    }

    // 标记已发布，记录最新数据
    ditem->published = true;
    pthread_mutex_lock(&ditem->ref_lock);
    ditem->info.ref_count++;
    pthread_mutex_unlock(&ditem->ref_lock);
    ctx->latest_item_held = ditem;

    // 【推模式核心】通知所有订阅该类型的消费者
    _data_bus_notify_subscribers(ctx, ditem);
    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

/**
 * @brief 订阅数据 → 消费者注册回调
 */
int data_bus_subscribe(data_bus_handle_t handle, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_t *out_sub) {
    if (!handle || !cb || !out_sub) return -1;
    data_bus_context_t *ctx = handle;

    pthread_mutex_lock(&ctx->lock);
    // 遍历订阅者数组，找空位注册
    for (uint32_t i=0; i<ctx->max_subscribers; i++) {
        if (!ctx->subscribers[i].valid) {
            ctx->subscribers[i].type = type;
            ctx->subscribers[i].cb = cb;
            ctx->subscribers[i].user_data = user_data;
            ctx->subscribers[i].valid = true;
            // 输出订阅句柄（指向订阅者结构体）
            *out_sub = &ctx->subscribers[i];
            pthread_mutex_unlock(&ctx->lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&ctx->lock);
    return -1;
}

// 取消订阅
int data_bus_unsubscribe(data_bus_handle_t handle, data_bus_subscription_t *sub) {
    if (!handle || !sub || !*sub) return -1;
    data_bus_context_t *ctx = handle;
    data_subscriber_t *s = *sub;

    pthread_mutex_lock(&ctx->lock);
    s->valid = false;
    pthread_mutex_unlock(&ctx->lock);
    *sub = NULL;
    return 0;
}

/**
 * @brief 拉模式：获取最新数据
 */
int data_bus_acquire_latest(data_bus_handle_t handle, data_type_t type,
                             data_bus_item_handle_t *out_item) {
    if (!handle || !out_item) return -1;
    data_bus_context_t *ctx = handle;
    pthread_rwlock_rdlock(&ctx->rwlock);

    // 修复：直接判断最新数据指针，替代原latest_item_index
    if (!ctx->latest_item_held) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return -1;
    }
    // 修复：直接使用最新数据指针，替代原数组索引
    data_item_t *item = ctx->latest_item_held;
    if (type != DATA_TYPE_INVALID && item->info.type != type) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return -1;
    }

    // 引用计数+1，保证数据安全
    pthread_mutex_lock(&item->ref_lock);
    item->info.ref_count++;
    pthread_mutex_unlock(&item->ref_lock);
    pthread_rwlock_unlock(&ctx->rwlock);
    *out_item = item;
    return 0;
}

// 获取只读指针
const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item) {
    return item ? ((data_item_t*)item)->data_ptr : NULL;
}

// 获取数据信息
int data_bus_get_item_info(data_bus_item_handle_t item, data_bus_item_info_t *out_info) {
    if (!item || !out_info) return -1;
    data_item_t *ditem = item;
    pthread_mutex_lock(&ditem->ref_lock);
    *out_info = ditem->info;
    pthread_mutex_unlock(&ditem->ref_lock);
    return 0;
}

/**
 * @brief 释放数据（引用计数-1）
 * 计数=0 → 内存回收
 */
int data_bus_release(data_bus_item_handle_t item) {
    if (!item) return -1;
    data_item_t *ditem = item;

    pthread_mutex_lock(&ditem->ref_lock);
    if (ditem->info.ref_count == 0) { pthread_mutex_unlock(&ditem->ref_lock); return -1; }
    ditem->info.ref_count--;
    uint32_t cnt = ditem->info.ref_count;
    pthread_mutex_unlock(&ditem->ref_lock);

    // 没人用了，重置数据项
    if (cnt == 0) _data_bus_reset_item(ditem);
    return 0;
}

// 销毁总线，释放所有内存
int data_bus_deinit(data_bus_handle_t handle) {
    if (!handle) return -1;
    data_bus_context_t *ctx = handle;

    pthread_rwlock_wrlock(&ctx->rwlock);
    if (ctx->latest_item_held) _data_bus_reset_item(ctx->latest_item_held);
    for (uint32_t i=0; i<ctx->max_items; i++) pthread_mutex_destroy(&ctx->items[i].ref_lock);

    // 释放所有内存
    free(ctx->memory_pool);
    free(ctx->items);
    free(ctx->subscribers);
    pthread_rwlock_unlock(&ctx->rwlock);

    pthread_rwlock_destroy(&ctx->rwlock);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    LOG_I("Data Bus: 销毁成功");
    return 0;
}

// 类型转字符串
const char* data_type_to_str(data_type_t type) {
    return (type < DATA_TYPE_MAX) ? g_data_type_str[type] : "UNKNOWN";
}

// ==========================================================================
// 内部辅助函数
// ==========================================================================
// 获取微秒级时间戳
static uint64_t _data_bus_get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// 查找空闲的数据项
static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx) {
    for (uint32_t i=0; i<ctx->max_items; i++)
        if (!ctx->items[i].in_use) return &ctx->items[i];
    return NULL;
}

// 重置数据项（标记为未使用）
static void _data_bus_reset_item(data_item_t *item) {
    if (!item) return;
    memset(&item->info, 0, sizeof(item->info));
    item->in_use = false;
    item->published = false;
}

// 生产者通知所有匹配的订阅者,生产者负责CPU消耗
static void _data_bus_notify_subscribers(data_bus_context_t *ctx, data_item_t *item) {
    for (uint32_t i=0; i<ctx->max_subscribers; i++) {
        data_subscriber_t *s = &ctx->subscribers[i];
        // 只通知订阅了该数据类型的有效订阅者
        if (s->valid && (s->type == DATA_TYPE_INVALID || s->type == item->info.type)) {
            // 引用+1 → 回调过程中数据不会被释放
            pthread_mutex_lock(&item->ref_lock);
            item->info.ref_count++;
            pthread_mutex_unlock(&item->ref_lock);
            // 执行消费者回调函数
            s->cb((data_bus_item_handle_t)item, s->user_data);
            // 回调完成，释放引用
            data_bus_release((data_bus_item_handle_t)item);
        }
    }
}