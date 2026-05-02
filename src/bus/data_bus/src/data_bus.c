// src/bus/data_bus/src/data_bus.c
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
#define DATA_BUS_MAX_ITEM_SIZE_DEFAULT (4 * 1024 * 1024) // 4MB

// ==========================================================================
// 内部数据项结构体
// ==========================================================================
typedef struct {
    data_bus_item_info_t info;
    void *data_ptr;          // 指向实际数据的指针
    bool in_use;             // 是否在使用中
    bool published;          // 是否已发布
    pthread_mutex_t ref_lock; // 引用计数锁
} data_item_t;

// ==========================================================================
// 内部上下文结构体
// ==========================================================================
typedef struct {
    data_bus_config_t config;
    data_item_t *items;
    uint32_t item_count;
    uint32_t max_items;
    size_t max_item_size;
    
    // 最新数据项索引（用于 acquire_latest）
    int latest_item_index;
    
    // 内存池（预分配）
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
// 内部辅助函数声明
// ==========================================================================
static uint64_t _data_bus_get_timestamp_us(void);
static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx);
static void _data_bus_reset_item(data_item_t *item);

// ==========================================================================
// 对外API实现
// ==========================================================================

int data_bus_init(const data_bus_config_t *config,
                  data_bus_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    data_bus_context_t *ctx = (data_bus_context_t*)malloc(sizeof(data_bus_context_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(data_bus_context_t));

    // 拷贝配置
    memcpy(&ctx->config, config, sizeof(data_bus_config_t));
    ctx->max_items = (config->max_items > 0) ? config->max_items : DATA_BUS_MAX_ITEMS_DEFAULT;
    ctx->max_item_size = (config->max_item_size > 0) ? config->max_item_size : DATA_BUS_MAX_ITEM_SIZE_DEFAULT;
    ctx->latest_item_index = -1;

    // 分配数据项数组
    ctx->items = (data_item_t*)malloc(ctx->max_items * sizeof(data_item_t));
    if (ctx->items == NULL) {
        free(ctx);
        return -1;
    }
    memset(ctx->items, 0, ctx->max_items * sizeof(data_item_t));

    // 预分配内存池（一次性分配，避免运行时碎片）
    size_t total_pool_size = ctx->max_items * ctx->max_item_size;
    ctx->memory_pool = malloc(total_pool_size);
    if (ctx->memory_pool == NULL) {
        free(ctx->items);
        free(ctx);
        return -1;
    }
    memset(ctx->memory_pool, 0, total_pool_size);

    // 初始化每个数据项
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        ctx->items[i].data_ptr = (uint8_t*)ctx->memory_pool + (i * ctx->max_item_size);
        ctx->items[i].in_use = false;
        ctx->items[i].published = false;
        pthread_mutex_init(&ctx->items[i].ref_lock, NULL);
    }

    pthread_mutex_init(&ctx->lock, NULL);
    pthread_rwlock_init(&ctx->rwlock, NULL);

    *out_handle = (data_bus_handle_t)ctx;
    LOG_I("Data Bus: Initialized, max_items=%u, max_item_size=%uKB", 
          ctx->max_items, ctx->max_item_size / 1024);
    return 0;
}

// -------------------------------------------------------------------------
// 生产者接口
// -------------------------------------------------------------------------

int data_bus_alloc(data_bus_handle_t handle,
                   data_type_t type,
                   size_t size,
                   const char *producer,
                   data_bus_item_handle_t *out_item)
{
    if (handle == NULL || out_item == NULL || type == DATA_TYPE_INVALID) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    if (size > ctx->max_item_size) {
        LOG_E("Data Bus: Alloc size %zu > max %zu", size, ctx->max_item_size);
        return -1;
    }

    pthread_mutex_lock(&ctx->lock);

    // 找一个空闲的 item
    data_item_t *item = _data_bus_find_free_item(ctx);
    if (item == NULL) {
        LOG_W("Data Bus: No free items available");
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    // 初始化 item
    _data_bus_reset_item(item);
    item->info.type = type;
    item->info.data_size = (uint32_t)size;
    item->info.timestamp = _data_bus_get_timestamp_us();
    item->info.producer = producer;
    item->info.ref_count = 1; // 生产者持有一个引用
    item->in_use = true;
    item->published = false;

    pthread_mutex_unlock(&ctx->lock);

    *out_item = (data_bus_item_handle_t)item;
    LOG_D("Data Bus: Allocated item (type=%s, size=%u)", data_type_to_str(type), (uint32_t)size);
    return 0;
}

void* data_bus_get_writable_ptr(data_bus_item_handle_t item)
{
    if (item == NULL) return NULL;
    data_item_t *ditem = (data_item_t*)item;

    // 只有未发布的 item 才能写
    if (ditem->published) {
        LOG_W("Data Bus: Attempt to write published item");
        return NULL;
    }

    return ditem->data_ptr;
}

int data_bus_publish(data_bus_handle_t handle, data_bus_item_handle_t item)
{
    if (handle == NULL || item == NULL) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;
    data_item_t *ditem = (data_item_t*)item;

    pthread_mutex_lock(&ctx->lock);

    if (!ditem->in_use || ditem->published) {
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    // 标记为已发布
    ditem->published = true;
    ditem->info.timestamp = _data_bus_get_timestamp_us(); // 更新发布时间

    // 更新最新 item 索引
    // 这里我们通过指针偏移计算索引
    int index = (int)((uint8_t*)ditem - (uint8_t*)ctx->items) / sizeof(data_item_t);
    ctx->latest_item_index = index;

    pthread_mutex_unlock(&ctx->lock);

    LOG_D("Data Bus: Published item (type=%s, producer=%s)", 
          data_type_to_str(ditem->info.type), 
          ditem->info.producer);
    return 0;
}

// -------------------------------------------------------------------------
// 消费者接口
// -------------------------------------------------------------------------

int data_bus_acquire_latest(data_bus_handle_t handle,
                             data_type_t type,
                             data_bus_item_handle_t *out_item)
{
    if (handle == NULL || out_item == NULL) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    pthread_rwlock_rdlock(&ctx->rwlock); // 读锁

    if (ctx->latest_item_index < 0) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return -1; // 没有数据
    }

    data_item_t *item = &ctx->items[ctx->latest_item_index];

    // 类型校验
    if (type != DATA_TYPE_INVALID && item->info.type != type) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return -1;
    }

    if (!item->in_use || !item->published) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return -1;
    }

    // 原子增加引用计数
    pthread_mutex_lock(&item->ref_lock);
    item->info.ref_count++;
    pthread_mutex_unlock(&item->ref_lock);

    pthread_rwlock_unlock(&ctx->rwlock);

    *out_item = (data_bus_item_handle_t)item;
    LOG_D("Data Bus: Acquired latest item (ref_count=%u)", item->info.ref_count);
    return 0;
}

const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item)
{
    if (item == NULL) return NULL;
    data_item_t *ditem = (data_item_t*)item;
    return ditem->data_ptr;
}

int data_bus_get_item_info(data_bus_item_handle_t item,
                           data_bus_item_info_t *out_info)
{
    if (item == NULL || out_info == NULL) {
        return -1;
    }
    data_item_t *ditem = (data_item_t*)item;
    
    pthread_mutex_lock(&ditem->ref_lock);
    *out_info = ditem->info;
    pthread_mutex_unlock(&ditem->ref_lock);
    
    return 0;
}

int data_bus_release(data_bus_item_handle_t item)
{
    if (item == NULL) {
        return -1;
    }
    data_item_t *ditem = (data_item_t*)item;

    pthread_mutex_lock(&ditem->ref_lock);

    if (ditem->info.ref_count == 0) {
        pthread_mutex_unlock(&ditem->ref_lock);
        return -1;
    }

    ditem->info.ref_count--;
    uint32_t new_count = ditem->info.ref_count;

    pthread_mutex_unlock(&ditem->ref_lock);

    // 如果引用计数为0，回收
    if (new_count == 0) {
        // 注意：这里我们不需要加总线的大锁，因为 item 已经没人用了
        _data_bus_reset_item(ditem);
        LOG_D("Data Bus: Item released and recycled");
    } else {
        LOG_D("Data Bus: Item released (ref_count=%u)", new_count);
    }

    return 0;
}

// -------------------------------------------------------------------------
// 管理接口
// -------------------------------------------------------------------------

int data_bus_deinit(data_bus_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    pthread_rwlock_wrlock(&ctx->rwlock);

    // 销毁所有 item 的锁
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        pthread_mutex_destroy(&ctx->items[i].ref_lock);
    }

    // 释放内存
    free(ctx->memory_pool);
    free(ctx->items);
    
    ctx->memory_pool = NULL;
    ctx->items = NULL;
    ctx->latest_item_index = -1;

    pthread_rwlock_unlock(&ctx->rwlock);

    pthread_rwlock_destroy(&ctx->rwlock);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);

    LOG_I("Data Bus: Deinitialized");
    return 0;
}

const char* data_type_to_str(data_type_t type)
{
    if (type >= DATA_TYPE_CUSTOM_BASE) return "CUSTOM";
    if (type < DATA_TYPE_MAX) return g_data_type_str[type];
    return "UNKNOWN";
}

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

static uint64_t _data_bus_get_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx)
{
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        if (!ctx->items[i].in_use) {
            return &ctx->items[i];
        }
    }
    return NULL;
}

static void _data_bus_reset_item(data_item_t *item)
{
    if (item == NULL) return;
    
    pthread_mutex_lock(&item->ref_lock);
    memset(&item->info, 0, sizeof(item->info));
    // 注意：不重置 data_ptr 指向，只清空内容
    if (item->data_ptr != NULL) {
        memset(item->data_ptr, 0, 128); // 只清空前128字节，避免大内存操作
    }
    item->in_use = false;
    item->published = false;
    pthread_mutex_unlock(&item->ref_lock);
}