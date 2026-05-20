/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           data_bus.c
 * @brief          嵌入式Linux 高性能零拷贝数据总线【V4.0 标准实现】
 * @details
 *  严格遵循V4.0开发准则：
 *  1. 四层架构：属于双总线中间层，仅依赖Main层基础组件
 *  2. C-OOP：不透明指针封装，内部结构完全隐藏
 *  3. 单一职责：每个函数只做一件事
 *  4. 防御性编程：全参数检查，魔法数校验，引用计数安全
 *  5. 线程安全：细粒度锁+原子操作，并发无竞争
 *
 * @author         luo
 * @date           2026
 ******************************************************************************
 */

#include "data_bus.h"
#include "log.h"
#include "mem_adapter.h"   // TLSF内存适配层
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>    // C11原子操作核心头文件

// ==========================================================================
// 全局配置与宏定义
// ==========================================================================

#ifdef __x86_64__
#define MEM_ALIGN_MASK  7  // 64位：8字节对齐
#else
#define MEM_ALIGN_MASK  3  // 32位：4字节对齐
#endif
#define ALIGN_UP(size)   (((size) + MEM_ALIGN_MASK) & ~MEM_ALIGN_MASK)

#define MAX_DATA_BUS            4
#define BUS_NAME_MAX_LEN        16
#define DATA_BUS_MAGIC          0xA55A5AA5u  // 总线魔法数
#define DATA_BUS_ITEM_MAGIC     0x5AA5A55Au  // 数据项魔法数

// 默认配置
#define DATA_BUS_MAX_ITEMS_DEFAULT         32
#define DATA_BUS_MAX_ITEM_SIZE_DEFAULT     (4*1024*1024)
#define DATA_BUS_MAX_SUBSCRIBERS_DEFAULT   16

// ==========================================================================
// 内部数据结构（完全私有，外部不可见）
// ==========================================================================

/**
 * @brief 数据元信息结构体
 */
struct data_bus_item_info {
    data_type_t type;
    uint64_t timestamp;
    uint32_t data_size;
    atomic_uint ref_count;    // C11原子引用计数
    const char *producer;
};

/**
 * @brief 数据项结构体
 */
typedef struct data_bus_item_t {
    void                 *data_ptr;      // 数据指针
    struct data_bus_item_info  info;     // 元信息
    uint32_t              magic;         // 魔法数(安全校验)
    bool                  in_use;        // 使用标记
    bool                  published;     // 发布标记
} data_item_t;

/**
 * @brief 订阅者结构体
 */
typedef struct data_bus_subscription_t {
    data_bus_callback_t cb;
    data_type_t type;
    void *user_data;
    bool valid;
} data_subscriber_t;

/**
 * @brief 总线上下文结构体（完全私有）
 */
typedef struct data_bus_t {
    data_item_t *items;             // 数据项数组
    data_subscriber_t *subscribers; // 订阅者数组
    void *memory_pool;              // 内存池基址
    data_item_t *latest_item_held;  // 拉模式最新数据项（总线持有引用）
    data_bus_config_t config;       // 总线配置
    pthread_mutex_t pool_lock;      // 内存池锁：保护items数组和内存分配
    pthread_mutex_t sub_lock;       // 订阅锁：保护subscribers数组
    pthread_mutex_t publish_lock;   // 发布锁：保护发布过程和latest_item_held更新
    pthread_rwlock_t rwlock;        // 读写锁：保护latest_item_held的读取
    size_t max_item_size;           // 最大数据尺寸
    uint32_t max_items;             // 最大项数
    uint32_t max_subscribers;       // 最大订阅数
    uint32_t magic;                 // 总线魔法数
} data_bus_context_t;

/**
 * @brief 总线实例表项
 */
typedef struct {
    data_bus_context_t *bus;
    char name[BUS_NAME_MAX_LEN];
    bool used;
} data_bus_entry_t;

// ==========================================================================
// 全局静态变量（完全私有）
// ==========================================================================
static data_bus_entry_t s_bus_table[MAX_DATA_BUS] = {0};
static pthread_mutex_t  s_table_lock = PTHREAD_MUTEX_INITIALIZER;

static const char* g_data_type_str[] = {
    [DATA_TYPE_INVALID] = "INVALID",
    [DATA_TYPE_VIDEO] = "VIDEO_FRAME",
    [DATA_TYPE_AI_RESULT] = "AI_RESULT",
    [DATA_TYPE_AUDIO_FRAME] = "AUDIO_FRAME",
};

// ==========================================================================
// 内部辅助函数声明（全部static）
// ==========================================================================
static uint64_t _data_bus_get_timestamp_us(void);
static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx);
static void _data_bus_reset_item(data_item_t *item);
static int _data_bus_release_item_safe(data_item_t *item);
static void _data_bus_notify_subscribers(data_bus_context_t *ctx, data_item_t *item);
static int _data_bus_update_latest_item(data_bus_context_t *ctx, data_item_t *new_item);
static data_bus_context_t* _data_bus_find_ctx(const char *name);
static int _data_bus_alloc_context(data_bus_context_t **out_ctx);
static int _data_bus_init_memory_pool(data_bus_context_t *ctx);
static int _data_bus_init_locks(data_bus_context_t *ctx);
static void _data_bus_destroy_locks(data_bus_context_t *ctx);

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

/**
 * @brief  获取微秒级时间戳（单调时钟）
 * @return 微秒级时间戳
 */
static uint64_t _data_bus_get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/**
 * @brief  查找空闲数据项
 * @param  ctx: 总线上下文
 * @return 空闲数据项指针，NULL表示无空闲
 * @note   必须持有pool_lock调用
 */
static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx) {
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        data_item_t *item = &ctx->items[i];
        // 双重校验：未使用 且 引用计数为0
        if (!item->in_use && atomic_load(&item->info.ref_count) == 0) {
            return item;
        }
    }
    return NULL;
}

/**
 * @brief  重置数据项到初始状态
 * @param  item: 数据项指针
 */
static void _data_bus_reset_item(data_item_t *item) {
    if (!item || item->magic != DATA_BUS_ITEM_MAGIC) {
        return;
    }

    memset(&item->info, 0, sizeof(item->info));
    item->in_use = false;
    item->published = false;
}

/**
 * @brief  安全释放数据项（防下溢）
 * @param  item: 数据项指针
 * @return DATA_BUS_OK 成功，负数失败
 */
static int _data_bus_release_item_safe(data_item_t *item) {
    if (!item || item->magic != DATA_BUS_ITEM_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // 先检查当前引用计数，防止下溢
    unsigned int current_ref = atomic_load(&item->info.ref_count);
    if (current_ref == 0) {
        LOG_E("[BUS REF] 引用计数下溢！item=%p, ref=0", item);
        return DATA_BUS_ERR_REF_UNDERFLOW;
    }

    // 原子减1，获取减之前的值
    unsigned int old_ref = atomic_fetch_sub(&item->info.ref_count, 1);
    LOG_D("[BUS REF] item=%p, ref=%d -> %d", item, old_ref, old_ref - 1);

    // 如果减之前是1，说明减完就是0，需要重置
    if (old_ref == 1) {
        _data_bus_reset_item(item);
    }

    return DATA_BUS_OK;
}

/**
 * @brief  通知所有订阅者（推模式）
 * @param  ctx: 总线上下文
 * @param  item: 数据项指针
 * @note   必须持有publish_lock调用
 */
static void _data_bus_notify_subscribers(data_bus_context_t *ctx, data_item_t *item) {
    data_subscriber_t *temp_sub[16]; // 栈上临时队列
    uint32_t temp_cnt = 0;

    // 🔒 第一步：全程持锁，仅收集需要通知的订阅者（极快）
    pthread_mutex_lock(&ctx->sub_lock);
    for (uint32_t i = 0; i < ctx->max_subscribers && temp_cnt < 16; i++) {
        data_subscriber_t *s = &ctx->subscribers[i];
        if (s->valid && (s->type == DATA_TYPE_INVALID || s->type == item->info.type)) {
            temp_sub[temp_cnt++] = s;
        }
    }
    pthread_mutex_unlock(&ctx->sub_lock);
    // 🔓 锁已释放，安全执行回调

    // 第二步：无锁执行回调
    for (uint32_t i = 0; i < temp_cnt; i++) {
        data_subscriber_t *s = temp_sub[i];
        s->cb((data_bus_item_handle_t)item, s->user_data);
    }
}

/**
 * @brief  更新拉模式最新数据项
 * @param  ctx: 总线上下文
 * @param  new_item: 新数据项指针
 * @return DATA_BUS_OK 成功
 * @note   必须持有publish_lock调用
 */
static int _data_bus_update_latest_item(data_bus_context_t *ctx, data_item_t *new_item) {
    // 先释放旧的最新项
    if (ctx->latest_item_held) {
        int ret = _data_bus_release_item_safe(ctx->latest_item_held);
        if (ret != DATA_BUS_OK) {
            LOG_W("[BUS PULL] 释放旧最新项失败: %d", ret);
        }
        ctx->latest_item_held = NULL;
    }

    // 新数据项增加引用（总线持有）
    atomic_fetch_add(&new_item->info.ref_count, 1);
    ctx->latest_item_held = new_item;

    return DATA_BUS_OK;
}

/**
 * @brief  查找总线实例
 * @param  name: 总线名称
 * @return 总线上下文指针，NULL表示不存在
 */
static data_bus_context_t* _data_bus_find_ctx(const char *name) {
    if (!name) {
        return NULL;
    }

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

/**
 * @brief  分配总线上下文
 * @param  out_ctx: 输出总线上下文指针
 * @return DATA_BUS_OK 成功，负数失败
 */
static int _data_bus_alloc_context(data_bus_context_t **out_ctx) {
    if (!out_ctx) {
        return DATA_BUS_ERR_PARAM;
    }

    *out_ctx = mem_calloc(1, sizeof(data_bus_context_t));
    if (!*out_ctx) {
        LOG_E("[BUS INIT] 分配总线上下文失败");
        return DATA_BUS_ERR_MEM;
    }

    (*out_ctx)->magic = DATA_BUS_MAGIC;
    return DATA_BUS_OK;
}

/**
 * @brief  初始化内存池
 * @param  ctx: 总线上下文
 * @return DATA_BUS_OK 成功，负数失败
 */
static int _data_bus_init_memory_pool(data_bus_context_t *ctx) {
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_PARAM;
    }

    // 分配数据项数组
    ctx->items = mem_calloc(ctx->max_items, sizeof(data_item_t));
    if (!ctx->items) {
        LOG_E("[BUS INIT] 分配数据项数组失败");
        return DATA_BUS_ERR_MEM;
    }

    // 分配订阅者数组
    ctx->subscribers = mem_calloc(ctx->max_subscribers, sizeof(data_subscriber_t));
    if (!ctx->subscribers) {
        LOG_E("[BUS INIT] 分配订阅者数组失败");
        mem_free(ctx->items);
        return DATA_BUS_ERR_MEM;
    }

    // 分配数据内存池
    ctx->memory_pool = mem_alloc(ctx->max_items * ctx->max_item_size);
    if (!ctx->memory_pool) {
        LOG_E("[BUS INIT] 分配数据内存池失败");
        mem_free(ctx->subscribers);
        mem_free(ctx->items);
        return DATA_BUS_ERR_MEM;
    }

    // 初始化数据项
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        ctx->items[i].magic = DATA_BUS_ITEM_MAGIC;
        ctx->items[i].data_ptr = (uint8_t*)ctx->memory_pool + i * ctx->max_item_size;
    }

    return DATA_BUS_OK;
}

/**
 * @brief  初始化所有锁
 * @param  ctx: 总线上下文
 * @return DATA_BUS_OK 成功，负数失败
 */
static int _data_bus_init_locks(data_bus_context_t *ctx) {
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_PARAM;
    }

    int ret;

    ret = pthread_mutex_init(&ctx->pool_lock, NULL);
    if (ret != 0) {
        LOG_E("[BUS INIT] 初始化pool_lock失败: %d", ret);
        return ret;
    }

    ret = pthread_mutex_init(&ctx->sub_lock, NULL);
    if (ret != 0) {
        LOG_E("[BUS INIT] 初始化sub_lock失败: %d", ret);
        pthread_mutex_destroy(&ctx->pool_lock);
        return ret;
    }

    ret = pthread_mutex_init(&ctx->publish_lock, NULL);
    if (ret != 0) {
        LOG_E("[BUS INIT] 初始化publish_lock失败: %d", ret);
        pthread_mutex_destroy(&ctx->sub_lock);
        pthread_mutex_destroy(&ctx->pool_lock);
        return ret;
    }

    ret = pthread_rwlock_init(&ctx->rwlock, NULL);
    if (ret != 0) {
        LOG_E("[BUS INIT] 初始化rwlock失败: %d", ret);
        pthread_mutex_destroy(&ctx->publish_lock);
        pthread_mutex_destroy(&ctx->sub_lock);
        pthread_mutex_destroy(&ctx->pool_lock);
        return ret;
    }

    return DATA_BUS_OK;
}

/**
 * @brief  销毁所有锁
 * @param  ctx: 总线上下文
 */
static void _data_bus_destroy_locks(data_bus_context_t *ctx) {
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return;
    }

    pthread_rwlock_destroy(&ctx->rwlock);
    pthread_mutex_destroy(&ctx->publish_lock);
    pthread_mutex_destroy(&ctx->sub_lock);
    pthread_mutex_destroy(&ctx->pool_lock);
}

// ==========================================================================
// 对外API实现
// ==========================================================================

int data_bus_init(const data_bus_config_t *config) {
    if (!config || !config->name || strlen(config->name) >= BUS_NAME_MAX_LEN) {
        return DATA_BUS_ERR_PARAM;
    }

    // 检查总线是否已存在
    if (_data_bus_find_ctx(config->name)) {
        LOG_E("Data Bus[%s]: 已存在", config->name);
        return DATA_BUS_ERR_EXIST;
    }

    pthread_mutex_lock(&s_table_lock);

    // 查找空闲实例槽位
    int free_idx = -1;
    for (int i = 0; i < MAX_DATA_BUS; i++) {
        if (!s_bus_table[i].used) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        pthread_mutex_unlock(&s_table_lock);
        LOG_E("Data Bus: 实例表已满");
        return DATA_BUS_ERR_FULL;
    }

    // 分配总线上下文
    data_bus_context_t *ctx = NULL;
    int ret = _data_bus_alloc_context(&ctx);
    if (ret != DATA_BUS_OK) {
        pthread_mutex_unlock(&s_table_lock);
        return ret;
    }

    // 加载配置
    ctx->config = *config;
    ctx->max_items = config->max_items ? config->max_items : DATA_BUS_MAX_ITEMS_DEFAULT;
    ctx->max_item_size = config->max_item_size ? config->max_item_size : DATA_BUS_MAX_ITEM_SIZE_DEFAULT;
    ctx->max_item_size = ALIGN_UP(ctx->max_item_size);
    ctx->max_subscribers = config->max_subscribers ? config->max_subscribers : DATA_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->latest_item_held = NULL;

    // 初始化内存池
    ret = _data_bus_init_memory_pool(ctx);
    if (ret != DATA_BUS_OK) {
        mem_free(ctx);
        pthread_mutex_unlock(&s_table_lock);
        return ret;
    }

    // 初始化锁
    ret = _data_bus_init_locks(ctx);
    if (ret != DATA_BUS_OK) {
        mem_free(ctx->memory_pool);
        mem_free(ctx->subscribers);
        mem_free(ctx->items);
        mem_free(ctx);
        pthread_mutex_unlock(&s_table_lock);
        return ret;
    }

    // 注册实例
    strncpy(s_bus_table[free_idx].name, config->name, BUS_NAME_MAX_LEN-1);
    s_bus_table[free_idx].name[BUS_NAME_MAX_LEN-1] = '\0';
    ctx->config.name = s_bus_table[free_idx].name;
    s_bus_table[free_idx].bus = ctx;
    s_bus_table[free_idx].used = true;

    pthread_mutex_unlock(&s_table_lock);

    LOG_I("Data Bus[%s]: 初始化成功 (max_items=%u, max_item_size=%zu, max_subscribers=%u)",
          config->name, ctx->max_items, ctx->max_item_size, ctx->max_subscribers);

    return DATA_BUS_OK;
}

int data_bus_alloc(const char *name,
                   data_type_t type,
                   size_t size,
                   const char *producer,
                   data_bus_item_handle_t *out_item) {
    // 参数检查
    if (!name || !out_item || type == DATA_TYPE_INVALID) {
        return DATA_BUS_ERR_PARAM;
    }

    // 查找总线
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // 检查数据大小
    if (size > ctx->max_item_size) {
        LOG_E("[BUS ALLOC] 数据大小超过限制: %zu > %zu", size, ctx->max_item_size);
        return DATA_BUS_ERR_PARAM;
    }

    // 申请空闲数据项
    pthread_mutex_lock(&ctx->pool_lock);
    data_item_t *item = _data_bus_find_free_item(ctx);
    if (!item) {
        // 内存池满，打印调试信息
        LOG_E("[BUS ALLOC] 内存池已满！总线: %s", name);
        for (uint32_t i = 0; i < ctx->max_items; i++) {
            data_item_t *it = &ctx->items[i];
            LOG_E("  Item[%u]: ref=%u, published=%d, in_use=%d",
                  i, atomic_load(&it->info.ref_count), it->published, it->in_use);
        }
        pthread_mutex_unlock(&ctx->pool_lock);
        return DATA_BUS_ERR_FULL;
    }

    // 初始化数据项
    _data_bus_reset_item(item);
    item->info.type = type;
    item->info.data_size = size;
    item->info.timestamp = _data_bus_get_timestamp_us();
    item->info.producer = producer;
    atomic_init(&item->info.ref_count, 1);  // 生产者初始引用
    item->in_use = true;
    item->published = false;

    pthread_mutex_unlock(&ctx->pool_lock);

    *out_item = (data_bus_item_handle_t)item;
    return DATA_BUS_OK;
}

void* data_bus_get_writable_ptr(data_bus_item_handle_t item) {
    data_item_t *ditem = (data_item_t *)item;
    if (!ditem || ditem->magic != DATA_BUS_ITEM_MAGIC || ditem->published) {
        return NULL;
    }
    return ditem->data_ptr;
}

int data_bus_push(const char *name, data_bus_item_handle_t item) {
    // 参数检查
    if (!name || !item) {
        return DATA_BUS_ERR_PARAM;
    }

    // 查找总线
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // 检查数据项
    data_item_t *ditem = (data_item_t *)item;
    if (ditem->magic != DATA_BUS_ITEM_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    pthread_mutex_lock(&ctx->publish_lock);

    // 检查状态
    if (!ditem->in_use || ditem->published) {
        pthread_mutex_unlock(&ctx->publish_lock);
        LOG_E("[BUS PUSH] 状态错误: in_use=%d, published=%d", ditem->in_use, ditem->published);
        return DATA_BUS_ERR_STATE;
    }

    // 标记为已发布
    ditem->published = true;

    // 1. 推模式：通知所有订阅者
    _data_bus_notify_subscribers(ctx, ditem);

    // 2. 拉模式：更新最新数据项
    _data_bus_update_latest_item(ctx, ditem);

    pthread_mutex_unlock(&ctx->publish_lock);

    LOG_D("[BUS PUSH] 数据发布成功: type=%s, size=%u, producer=%s",
          data_type_to_str(ditem->info.type), ditem->info.data_size, ditem->info.producer);

    return DATA_BUS_OK;
}

// 推模式专用：引用+1
int data_bus_push_acquire(data_bus_item_handle_t item)
{
    data_item_t *ditem = (data_item_t *)item;
    if (!ditem || ditem->magic != DATA_BUS_ITEM_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // 原子安全+1
    atomic_fetch_add(&ditem->info.ref_count, 1);
    LOG_D("[BUS PUSH] 引用+1 item=%p, ref=%u",
          ditem, atomic_load(&ditem->info.ref_count));

    return DATA_BUS_OK;
}


int data_bus_subscribe(const char *name, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_handle_t *out_sub) {
    // 参数检查
    if (!name || !cb || !out_sub) {
        return DATA_BUS_ERR_PARAM;
    }

    // 查找总线
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    pthread_mutex_lock(&ctx->sub_lock);

    // 查找空闲订阅槽位
    for (uint32_t i = 0; i < ctx->max_subscribers; i++) {
        if (!ctx->subscribers[i].valid) {
            ctx->subscribers[i].type = type;
            ctx->subscribers[i].cb = cb;
            ctx->subscribers[i].user_data = user_data;
            ctx->subscribers[i].valid = true;
            *out_sub = (data_bus_subscription_handle_t)&ctx->subscribers[i];
            pthread_mutex_unlock(&ctx->sub_lock);
            LOG_I("[BUS SUB] 订阅成功: 总线=%s, 类型=%s", name, data_type_to_str(type));
            return DATA_BUS_OK;
        }
    }

    pthread_mutex_unlock(&ctx->sub_lock);
    LOG_E("[BUS SUB] 订阅表已满: 总线=%s", name);
    return DATA_BUS_ERR_FULL;
}

int data_bus_unsubscribe(const char *name, data_bus_subscription_handle_t *sub) {
    // 参数检查
    if (!name || !sub || !*sub) {
        return DATA_BUS_ERR_PARAM;
    }

    // 查找总线
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    data_subscriber_t *s = (data_subscriber_t *)*sub;

    pthread_mutex_lock(&ctx->sub_lock);
    s->valid = false;
    pthread_mutex_unlock(&ctx->sub_lock);

    *sub = NULL;
    LOG_I("[BUS SUB] 取消订阅成功: 总线=%s", name);
    return DATA_BUS_OK;
}

int data_bus_pull_latest(const char *name,
                         data_type_t type,
                         data_bus_item_handle_t *out_item) {
    // 参数检查
    if (!name || !out_item) {
        return DATA_BUS_ERR_PARAM;
    }

    // 查找总线
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // 加读锁获取最新项
    pthread_rwlock_rdlock(&ctx->rwlock);

    if (!ctx->latest_item_held) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return DATA_BUS_ERR_NO_DATA;
    }

    data_item_t *item = ctx->latest_item_held;

    // 检查数据类型
    if (type != DATA_TYPE_INVALID && item->info.type != type) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return DATA_BUS_ERR_TYPE;
    }

    // 增加引用计数
    atomic_fetch_add(&item->info.ref_count, 1);

    pthread_rwlock_unlock(&ctx->rwlock);

    *out_item = (data_bus_item_handle_t)item;
    return DATA_BUS_OK;
}

const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item) {
    data_item_t *ditem = (data_item_t *)item;
    if (!ditem || ditem->magic != DATA_BUS_ITEM_MAGIC) {
        return NULL;
    }
    return ditem->data_ptr;
}

int data_bus_release(data_bus_item_handle_t item) {
    if (!item) {
        return DATA_BUS_ERR_PARAM;
    }

    return _data_bus_release_item_safe((data_item_t *)item);
}

int data_bus_deinit(const char *name) {
    if (!name) {
        return DATA_BUS_ERR_PARAM;
    }

    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // 加写锁防止并发访问
    pthread_rwlock_wrlock(&ctx->rwlock);

    // 释放最新项
    if (ctx->latest_item_held) {
        _data_bus_release_item_safe(ctx->latest_item_held);
        ctx->latest_item_held = NULL;
    }

    // 释放所有数据项（强制重置）
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        _data_bus_reset_item(&ctx->items[i]);
    }

    // 释放内存
    mem_free(ctx->memory_pool);
    mem_free(ctx->items);
    mem_free(ctx->subscribers);

    pthread_rwlock_unlock(&ctx->rwlock);

    // 销毁锁
    _data_bus_destroy_locks(ctx);

    // 释放上下文
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
    return DATA_BUS_OK;
}

const char* data_type_to_str(data_type_t type) {
    return (type < DATA_TYPE_MAX && g_data_type_str[type]) ? g_data_type_str[type] : "UNKNOWN";
}