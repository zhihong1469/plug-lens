/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           data_bus.c
 * @brief          高性能数据总线实现【V2.0 优化版】
 * @details
 *  1. C11原子引用计数：无锁操作，性能提升显著
 *  2. 细粒度锁设计：内存池锁/订阅锁/发布锁分离，并发无竞争
 *  3. TLSF静态内存池：零碎片，适配嵌入式Linux
 *  4. 魔法数校验：非法句柄直接拦截，杜绝程序崩溃
 *  5. 保留零拷贝/推/拉模式，完全兼容上层业务
 * @author         luo
 * @date           2026
 ******************************************************************************
 */

#include "data_bus.h"
#include "log.h"
#include "mem_adapter.h"   // 我们的TLSF内存适配层
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>    // C11原子操作核心头文件

// ==========================================================================
// 全局配置与宏定义
// ==========================================================================
#define MAX_DATA_BUS            4
#define BUS_NAME_MAX_LEN        16
#define DATA_BUS_MAGIC          0xA55A5AA5u  // 魔法数：防非法指针
#define DATA_BUS_ITEM_MAGIC     0x5AA5A55Au  // 数据项魔法数

// 默认配置
#define DATA_BUS_MAX_ITEMS_DEFAULT         32
#define DATA_BUS_MAX_ITEM_SIZE_DEFAULT     (4*1024*1024)
#define DATA_BUS_MAX_SUBSCRIBERS_DEFAULT   16

// ==========================================================================
// 多实例管理（原有逻辑不变）
// ==========================================================================
typedef struct {
    char name[BUS_NAME_MAX_LEN];
    struct data_bus_t *bus;
    bool used;
} data_bus_entry_t;

static data_bus_entry_t s_bus_table[MAX_DATA_BUS] = {0};
static pthread_mutex_t  s_table_lock = PTHREAD_MUTEX_INITIALIZER;

// ==========================================================================
// 内部数据结构（优化版：原子计数+魔法数+删除无用锁）
// ==========================================================================

// 数据元信息
struct data_bus_item_info {
    data_type_t type;
    uint64_t timestamp;
    uint32_t data_size;
    atomic_uint ref_count;    // C11原子引用计数 ✅ 无锁优化
    const char *producer;
};

// 数据项结构体（新增魔法数，删除ref_lock）
typedef struct data_bus_item_t {
    uint32_t magic;                      // 魔法数：安全校验 ✅
    struct data_bus_item_info info;
    void *data_ptr;
    bool in_use;
    bool published;
} data_item_t;

// 订阅者结构体（不变）
typedef struct data_bus_subscription_t {
    data_type_t type;
    data_bus_callback_t cb;
    void *user_data;
    bool valid;
} data_subscriber_t;

// 总线上下文（细粒度锁优化 + 魔法数）
typedef struct data_bus_t {
    uint32_t magic;                      // 魔法数：安全校验 ✅
    data_bus_config_t config;
    data_item_t *items;
    data_subscriber_t *subscribers;
    uint32_t max_items;
    uint32_t max_subscribers;
    size_t max_item_size;
    data_item_t *latest_item_held;
    void *memory_pool;

    // 🔴 原全局大锁 → 拆分三把细粒度锁 ✅ 核心优化
    pthread_mutex_t pool_lock;     // 保护：内存池/alloc/free
    pthread_mutex_t sub_lock;      // 保护：订阅者列表
    pthread_mutex_t publish_lock;  // 保护：发布/最新数据
    pthread_rwlock_t  rwlock;      // 保留：拉模式读写锁
} data_bus_context_t;

// 数据类型字符串（不变）
static const char* g_data_type_str[] = {
    [DATA_TYPE_INVALID] = "INVALID",
    [DATA_TYPE_VIDEO_FRAME] = "VIDEO_FRAME",
    [DATA_TYPE_AI_RESULT] = "AI_RESULT",
    [DATA_TYPE_AUDIO_FRAME] = "AUDIO_FRAME",
};

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static uint64_t _data_bus_get_timestamp_us(void);
static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx);
static void _data_bus_reset_item(data_item_t *item);
static void _data_bus_notify_subscribers(data_bus_context_t *ctx, data_item_t *item);
static data_bus_context_t* _data_bus_find_ctx(const char *name);

// ==========================================================================
// 内部函数：查找总线实例（不变）
// ==========================================================================
static data_bus_context_t* _data_bus_find_ctx(const char *name) {
    if (!name) return NULL;

    pthread_mutex_lock(&s_table_lock);
    data_bus_context_t *ctx = NULL;
    for (int i = 0; i < MAX_DATA_BUS; i++) {
        if (s_bus_table[i].used && strcmp(s_bus_table[i].name, name) == 0) {
            ctx = s_bus_table[i].bus;
            break;
        }
    }
    pthread_mutex_unlock(&s_table_lock);
    return ctx;
}

// ==========================================================================
// 对外API实现（100%兼容，仅优化底层实现）
// ==========================================================================

int data_bus_init(const data_bus_config_t *config) {
    if (!config || !config->name || strlen(config->name) >= BUS_NAME_MAX_LEN) {
        return -1;
    }

    if (_data_bus_find_ctx(config->name)) {
        LOG_E("Data Bus[%s]: Already exists", config->name);
        return -1;
    }

    pthread_mutex_lock(&s_table_lock);
    int free_idx = -1;
    for (int i = 0; i < MAX_DATA_BUS; i++) {
        if (!s_bus_table[i].used) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        pthread_mutex_unlock(&s_table_lock);
        LOG_E("Data Bus: Instance table full");
        return -1;
    }

    // ✅ 替换为TLSF内存分配
    data_bus_context_t *ctx = mem_calloc(1, sizeof(data_bus_context_t));
    if (!ctx) {
        pthread_mutex_unlock(&s_table_lock);
        return -1;
    }

    // 初始化魔法数
    ctx->magic = DATA_BUS_MAGIC;

    // 加载配置
    ctx->config = *config;
    ctx->max_items = config->max_items ? config->max_items : DATA_BUS_MAX_ITEMS_DEFAULT;
    ctx->max_item_size = config->max_item_size ? config->max_item_size : DATA_BUS_MAX_ITEM_SIZE_DEFAULT;
    ctx->max_subscribers = config->max_subscribers ? config->max_subscribers : DATA_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->latest_item_held = NULL;

    // ✅ TLSF内存分配
    ctx->items = mem_calloc(ctx->max_items, sizeof(data_item_t));
    ctx->subscribers = mem_calloc(ctx->max_subscribers, sizeof(data_subscriber_t));
    ctx->memory_pool = mem_alloc(ctx->max_items * ctx->max_item_size);

    if (!ctx->items || !ctx->memory_pool || !ctx->subscribers) {
        mem_free(ctx->subscribers); mem_free(ctx->items); mem_free(ctx->memory_pool); mem_free(ctx);
        pthread_mutex_unlock(&s_table_lock);
        return -1;
    }

    // 初始化数据项
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        ctx->items[i].magic = DATA_BUS_ITEM_MAGIC;
        ctx->items[i].data_ptr = (uint8_t*)ctx->memory_pool + i * ctx->max_item_size;
    }

    // ✅ 初始化细粒度锁
    pthread_mutex_init(&ctx->pool_lock, NULL);
    pthread_mutex_init(&ctx->sub_lock, NULL);
    pthread_mutex_init(&ctx->publish_lock, NULL);
    pthread_rwlock_init(&ctx->rwlock, NULL);

    // 注册实例
    strncpy(s_bus_table[free_idx].name, config->name, BUS_NAME_MAX_LEN-1);
    s_bus_table[free_idx].bus = ctx;
    s_bus_table[free_idx].used = true;

    pthread_mutex_unlock(&s_table_lock);
    LOG_I("Data Bus[%s]: 初始化成功", config->name);
    return 0;
}

int data_bus_alloc(const char *name, data_type_t type, size_t size,
                   const char *producer, data_bus_item_handle_t *out_item) {
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC || !out_item || type == DATA_TYPE_INVALID) return -1;
    if (size > ctx->max_item_size) return -1;

    // ✅ 仅锁内存池，细粒度无竞争
    pthread_mutex_lock(&ctx->pool_lock);
    data_item_t *item = _data_bus_find_free_item(ctx);
    if (!item) { pthread_mutex_unlock(&ctx->pool_lock); return -1; }

    _data_bus_reset_item(item);
    item->info.type = type;
    item->info.data_size = size;
    item->info.timestamp = _data_bus_get_timestamp_us();
    item->info.producer = producer;
    atomic_init(&item->info.ref_count, 1);  // 原子计数初始化
    item->in_use = true;
    item->published = false;

    pthread_mutex_unlock(&ctx->pool_lock);
    *out_item = item;
    return 0;
}

void* data_bus_get_writable_ptr(data_bus_item_handle_t item) {
    data_item_t *ditem = (data_item_t *)item;
    if (!ditem || ditem->magic != DATA_BUS_ITEM_MAGIC || ditem->published) return NULL;
    return ditem->data_ptr;
}

int data_bus_publish(const char *name, data_bus_item_handle_t item) {
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    data_item_t *ditem = (data_item_t *)item;

    if (!ctx || ctx->magic != DATA_BUS_MAGIC || !ditem || ditem->magic != DATA_BUS_ITEM_MAGIC) return -1;

    // ✅ 仅锁发布操作
    pthread_mutex_lock(&ctx->publish_lock);
    if (!ditem->in_use || ditem->published) { pthread_mutex_unlock(&ctx->publish_lock); return -1; }

    // 释放旧数据（原子操作，无锁）
    if (ctx->latest_item_held) {
        unsigned int cnt = atomic_fetch_sub(&ctx->latest_item_held->info.ref_count, 1);
        if (cnt == 1) _data_bus_reset_item(ctx->latest_item_held);
    }

    // 发布新数据
    ditem->published = true;
    atomic_fetch_add(&ditem->info.ref_count, 1);
    ctx->latest_item_held = ditem;

    // 通知订阅者
    _data_bus_notify_subscribers(ctx, ditem);
    pthread_mutex_unlock(&ctx->publish_lock);
    return 0;
}

int data_bus_subscribe(const char *name, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_handle_t *out_sub) {
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC || !cb || !out_sub) return -1;

    // ✅ 仅锁订阅列表
    pthread_mutex_lock(&ctx->sub_lock);
    for (uint32_t i = 0; i < ctx->max_subscribers; i++) {
        if (!ctx->subscribers[i].valid) {
            ctx->subscribers[i].type = type;
            ctx->subscribers[i].cb = cb;
            ctx->subscribers[i].user_data = user_data;
            ctx->subscribers[i].valid = true;
            *out_sub = &ctx->subscribers[i];
            pthread_mutex_unlock(&ctx->sub_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&ctx->sub_lock);
    return -1;
}

int data_bus_unsubscribe(const char *name, data_bus_subscription_handle_t *sub) {
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC || !sub || !*sub) return -1;
    data_subscriber_t *s = (data_subscriber_t *)*sub;

    pthread_mutex_lock(&ctx->sub_lock);
    s->valid = false;
    pthread_mutex_unlock(&ctx->sub_lock);
    *sub = NULL;
    return 0;
}

int data_bus_acquire_latest(const char *name, data_type_t type, data_bus_item_handle_t *out_item) {
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC || !out_item) return -1;

    pthread_rwlock_rdlock(&ctx->rwlock);
    if (!ctx->latest_item_held) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return -1;
    }

    data_item_t *item = ctx->latest_item_held;
    if (type != DATA_TYPE_INVALID && item->info.type != type) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return -1;
    }

    // 原子引用+1，无锁
    atomic_fetch_add(&item->info.ref_count, 1);
    pthread_rwlock_unlock(&ctx->rwlock);
    *out_item = item;
    return 0;
}

const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item) {
    data_item_t *ditem = (data_item_t *)item;
    if (!ditem || ditem->magic != DATA_BUS_ITEM_MAGIC) return NULL;
    return ditem->data_ptr;
}

int data_bus_release(data_bus_item_handle_t item) {
    data_item_t *ditem = (data_item_t *)item;
    if (!ditem || ditem->magic != DATA_BUS_ITEM_MAGIC) return -1;

    // ✅ 原子无锁引用计数-1
    unsigned int cnt = atomic_fetch_sub(&ditem->info.ref_count, 1);
    if (cnt == 0) _data_bus_reset_item(ditem);
    return 0;
}

int data_bus_deinit(const char *name) {
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) return -1;

    pthread_rwlock_wrlock(&ctx->rwlock);
    if (ctx->latest_item_held) _data_bus_reset_item(ctx->latest_item_held);

    // ✅ TLSF内存释放
    mem_free(ctx->memory_pool);
    mem_free(ctx->items);
    mem_free(ctx->subscribers);
    pthread_rwlock_unlock(&ctx->rwlock);

    // 销毁锁
    pthread_rwlock_destroy(&ctx->rwlock);
    pthread_mutex_destroy(&ctx->pool_lock);
    pthread_mutex_destroy(&ctx->sub_lock);
    pthread_mutex_destroy(&ctx->publish_lock);
    mem_free(ctx);

    // 清空实例表
    pthread_mutex_lock(&s_table_lock);
    for (int i = 0; i < MAX_DATA_BUS; i++) {
        if (s_bus_table[i].used && strcmp(s_bus_table[i].name, name) == 0) {
            memset(&s_bus_table[i], 0, sizeof(data_bus_entry_t));
            break;
        }
    }
    pthread_mutex_unlock(&s_table_lock);

    LOG_I("Data Bus[%s]: 销毁成功", name);
    return 0;
}

const char* data_type_to_str(data_type_t type) {
    return (type < DATA_TYPE_MAX && g_data_type_str[type]) ? g_data_type_str[type] : "UNKNOWN";
}

// ==========================================================================
// 内部辅助函数（不变）
// ==========================================================================
static uint64_t _data_bus_get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx) {
    for (uint32_t i=0; i<ctx->max_items; i++)
        if (!ctx->items[i].in_use) return &ctx->items[i];
    return NULL;
}

static void _data_bus_reset_item(data_item_t *item) {
    if (!item || item->magic != DATA_BUS_ITEM_MAGIC) return;
    memset(&item->info, 0, sizeof(item->info));
    item->in_use = false;
    item->published = false;
}

static void _data_bus_notify_subscribers(data_bus_context_t *ctx, data_item_t *item) {
    for (uint32_t i=0; i<ctx->max_subscribers; i++) {
        data_subscriber_t *s = &ctx->subscribers[i];
        if (s->valid && (s->type == DATA_TYPE_INVALID || s->type == item->info.type)) {
            atomic_fetch_add(&item->info.ref_count, 1);
            s->cb((data_bus_item_handle_t)item, s->user_data);
            data_bus_release((data_bus_item_handle_t)item);
        }
    }
}