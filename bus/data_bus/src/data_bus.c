/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           data_bus.c
 * @brief          High-Performance Zero-Copy Data Bus for Embedded Linux [V4.0 Standard]
 * @author         LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 *
 * @details
 *  Strictly follow V4.0 Development Rules:
 *  1. 4-Layer Architecture: Dual-bus middle layer, depends only on Main basic components
 *  2. C-OOP: Opaque pointer encapsulation, fully hidden internal structure
 *  3. Single Responsibility: One function, one job
 *  4. Defensive Programming: Full parameter check, magic verify, safe refcount
 *  5. Thread-Safe: Fine-grained lock + atomic operations, no concurrency race
 *
 ******************************************************************************
 */

#include "data_bus.h"
#include "log.h"
#include "mem_adapter.h"   // TLSF Memory Adapter
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>    // C11 Atomic Operations

// ==========================================================================
// Global Config & Macros
// ==========================================================================

#ifdef __x86_64__
#define MEM_ALIGN_MASK  7  /**< 64-bit system: 8-byte memory alignment */
#else
#define MEM_ALIGN_MASK  3  /**< 32-bit system: 4-byte memory alignment */
#endif
#define ALIGN_UP(size)   (((size) + MEM_ALIGN_MASK) & ~MEM_ALIGN_MASK) /**< Align size up */

#define MAX_DATA_BUS            4           /**< Max supported bus instances */
#define BUS_NAME_MAX_LEN        16          /**< Max length of bus name */
#define DATA_BUS_MAGIC          0xA55A5AA5u  /**< Bus instance magic number (safety check) */
#define DATA_BUS_ITEM_MAGIC     0x5AA5A55Au  /**< Data item magic number (safety check) */

// Default Configuration Parameters
#define DATA_BUS_MAX_ITEMS_DEFAULT         32                  /**< Default max items */
#define DATA_BUS_MAX_ITEM_SIZE_DEFAULT     (4*1024*1024)       /**< Default max item size (4MB) */
#define DATA_BUS_MAX_SUBSCRIBERS_DEFAULT   16                  /**< Default max subscribers */

// ==========================================================================
// Internal Data Structures (Fully Private, Encapsulated)
// ==========================================================================

/**
 * @brief Data Meta Information Structure
 */
struct data_bus_item_info {
    data_type_t type;                /**< Data type */
    uint64_t timestamp;              /**< Monotonic timestamp (us) */
    uint32_t data_size;              /**< Valid data size */
    atomic_uint ref_count;           /**< C11 Atomic reference count */
    const char *producer;            /**< Producer ID string */
};

/**
 * @brief Data Item Core Structure (Private)
 */
typedef struct data_bus_item_t {
    void                 *data_ptr;      /**< Pointer to data buffer */
    struct data_bus_item_info  info;     /**< Meta information */
    uint32_t              magic;         /**< Magic number for safety check */
    bool                  in_use;        /**< Item in-use flag */
    bool                  published;     /**< Item published flag */
} data_item_t;

/**
 * @brief Subscriber Structure (Private)
 */
typedef struct data_bus_subscription_t {
    data_bus_callback_t cb;            /**< Callback function */
    data_type_t type;                   /**< Subscribed data type */
    void *user_data;                    /**< Private user data */
    bool valid;                         /**< Valid subscriber flag */
} data_subscriber_t;

/**
 * @brief Bus Context Structure (Fully Private, Core)
 */
typedef struct data_bus_t {
    data_item_t *items;             /**< Data item array */
    data_subscriber_t *subscribers; /**< Subscriber array */
    void *memory_pool;              /**< TLSF memory pool base address */
    data_item_t *latest_item_held;  /**< Pull-mode latest item (bus holds ref) */
    data_bus_config_t config;       /**< User configuration */
    pthread_mutex_t pool_lock;      /**< Pool lock: protect items & allocation */
    pthread_mutex_t sub_lock;       /**< Sub lock: protect subscribers */
    pthread_mutex_t publish_lock;   /**< Publish lock: protect publish & latest update */
    pthread_rwlock_t rwlock;        /**< Rwlock: protect latest item read */
    size_t max_item_size;           /**< Max single data size */
    uint32_t max_items;             /**< Max item count */
    uint32_t max_subscribers;       /**< Max subscriber count */
    uint32_t magic;                 /**< Bus magic number */
} data_bus_context_t;

/**
 * @brief Bus Instance Table Entry (Private)
 */
typedef struct {
    data_bus_context_t *bus;
    char name[BUS_NAME_MAX_LEN];
    bool used;
} data_bus_entry_t;

// ==========================================================================
// Global Static Variables (Fully Private)
// ==========================================================================
static data_bus_entry_t s_bus_table[MAX_DATA_BUS] = {0};
static pthread_mutex_t  s_table_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Data type to string mapping table (log use)
 */
static const char* g_data_type_str[] = {
    [DATA_TYPE_INVALID] = "INVALID",
    [DATA_TYPE_VIDEO] = "VIDEO_FRAME",
    [DATA_TYPE_AI_RESULT] = "AI_RESULT",
    [DATA_TYPE_AUDIO_FRAME] = "AUDIO_FRAME",
};

// ==========================================================================
// Internal Helper Functions (All Static, Private)
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
// Internal Helper Functions Implementation
// ==========================================================================

/**
 * @brief  Get system monotonic timestamp (microseconds)
 * @return 64-bit timestamp value
 */
static uint64_t _data_bus_get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/**
 * @brief  Find free data item from pool
 * @param  ctx  Bus context pointer
 * @return Free item pointer, NULL if pool full
 * @note   MUST hold pool_lock before calling this function
 */
static data_item_t* _data_bus_find_free_item(data_bus_context_t *ctx) {
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        data_item_t *item = &ctx->items[i];
        // Double check: unused AND refcount == 0
        if (!item->in_use && atomic_load(&item->info.ref_count) == 0) {
            return item;
        }
    }
    return NULL;
}

/**
 * @brief  Reset data item to initial state
 * @param  item  Data item pointer
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
 * @brief  Safely release data item (refcount underflow protection)
 * @param  item  Data item pointer
 * @return DATA_BUS_OK on success, negative error code on failure
 */
static int _data_bus_release_item_safe(data_item_t *item) {
    if (!item || item->magic != DATA_BUS_ITEM_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // Check ref first to prevent underflow
    unsigned int current_ref = atomic_load(&item->info.ref_count);
    if (current_ref == 0) {
        LOG_E("[BUS REF] Refcount underflow! item=%p, ref=0", item);
        return DATA_BUS_ERR_REF_UNDERFLOW;
    }

    // Atomic decrement, get old value
    unsigned int old_ref = atomic_fetch_sub(&item->info.ref_count, 1);

    // Reset item if refcount reaches 0
    if (old_ref == 1) {
        _data_bus_reset_item(item);
    }

    return DATA_BUS_OK;
}

/**
 * @brief  Notify all valid subscribers (Push Mode)
 * @param  ctx   Bus context pointer
 * @param  item  Data item pointer
 * @note   MUST hold publish_lock before calling this function
 */
static void _data_bus_notify_subscribers(data_bus_context_t *ctx, data_item_t *item) {
    data_subscriber_t *temp_sub[16]; /**< Stack temp queue for subscribers */
    uint32_t temp_cnt = 0;

    // Lock only for fast subscriber collection (minimize critical section)
    pthread_mutex_lock(&ctx->sub_lock);
    for (uint32_t i = 0; i < ctx->max_subscribers && temp_cnt < 16; i++) {
        data_subscriber_t *s = &ctx->subscribers[i];
        if (s->valid && (s->type == DATA_TYPE_INVALID || s->type == item->info.type)) {
            temp_sub[temp_cnt++] = s;
        }
    }
    pthread_mutex_unlock(&ctx->sub_lock);

    // Execute callbacks without lock (safe for async processing)
    for (uint32_t i = 0; i < temp_cnt; i++) {
        data_subscriber_t *s = temp_sub[i];
        s->cb((data_bus_item_handle_t)item, s->user_data);
    }
}

/**
 * @brief  Update Pull-Mode latest data item
 * @param  ctx       Bus context pointer
 * @param  new_item  New data item pointer
 * @return DATA_BUS_OK on success
 * @note   MUST hold publish_lock before calling this function
 */
static int _data_bus_update_latest_item(data_bus_context_t *ctx, data_item_t *new_item) {
    // Release old latest item reference
    if (ctx->latest_item_held) {
        int ret = _data_bus_release_item_safe(ctx->latest_item_held);
        if (ret != DATA_BUS_OK) {
            LOG_W("[BUS PULL] Release old latest item failed: %d", ret);
        }
        ctx->latest_item_held = NULL;
    }

    // Add reference for new item (bus holds ownership)
    atomic_fetch_add(&new_item->info.ref_count, 1);
    ctx->latest_item_held = new_item;

    return DATA_BUS_OK;
}

/**
 * @brief  Find bus instance by name
 * @param  name  Bus instance name
 * @return Bus context pointer, NULL if not found
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
 * @brief  Allocate bus context memory
 * @param  out_ctx  Output bus context pointer
 * @return DATA_BUS_OK on success, negative error code on failure
 */
static int _data_bus_alloc_context(data_bus_context_t **out_ctx) {
    if (!out_ctx) {
        return DATA_BUS_ERR_PARAM;
    }

    *out_ctx = mem_calloc(1, sizeof(data_bus_context_t));
    if (!*out_ctx) {
        LOG_E("[BUS INIT] Allocate bus context failed");
        return DATA_BUS_ERR_MEM;
    }

    (*out_ctx)->magic = DATA_BUS_MAGIC;
    return DATA_BUS_OK;
}

/**
 * @brief  Initialize TLSF memory pool and arrays
 * @param  ctx  Bus context pointer
 * @return DATA_BUS_OK on success, negative error code on failure
 */
static int _data_bus_init_memory_pool(data_bus_context_t *ctx) {
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_PARAM;
    }

    // Allocate item array
    ctx->items = mem_calloc(ctx->max_items, sizeof(data_item_t));
    if (!ctx->items) {
        LOG_E("[BUS INIT] Allocate item array failed");
        return DATA_BUS_ERR_MEM;
    }

    // Allocate subscriber array
    ctx->subscribers = mem_calloc(ctx->max_subscribers, sizeof(data_subscriber_t));
    if (!ctx->subscribers) {
        LOG_E("[BUS INIT] Allocate subscriber array failed");
        mem_free(ctx->items);
        return DATA_BUS_ERR_MEM;
    }

    // Allocate data memory pool
    ctx->memory_pool = mem_alloc(ctx->max_items * ctx->max_item_size);
    if (!ctx->memory_pool) {
        LOG_E("[BUS INIT] Allocate data pool failed");
        mem_free(ctx->subscribers);
        mem_free(ctx->items);
        return DATA_BUS_ERR_MEM;
    }

    // Initialize item magic and data pointer
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        ctx->items[i].magic = DATA_BUS_ITEM_MAGIC;
        ctx->items[i].data_ptr = (uint8_t*)ctx->memory_pool + i * ctx->max_item_size;
    }

    return DATA_BUS_OK;
}

/**
 * @brief  Initialize all synchronization locks
 * @param  ctx  Bus context pointer
 * @return DATA_BUS_OK on success, negative error code on failure
 */
static int _data_bus_init_locks(data_bus_context_t *ctx) {
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_PARAM;
    }

    int ret;

    ret = pthread_mutex_init(&ctx->pool_lock, NULL);
    if (ret != 0) {
        LOG_E("[BUS INIT] Init pool_lock failed: %d", ret);
        return ret;
    }

    ret = pthread_mutex_init(&ctx->sub_lock, NULL);
    if (ret != 0) {
        LOG_E("[BUS INIT] Init sub_lock failed: %d", ret);
        pthread_mutex_destroy(&ctx->pool_lock);
        return ret;
    }

    ret = pthread_mutex_init(&ctx->publish_lock, NULL);
    if (ret != 0) {
        LOG_E("[BUS INIT] Init publish_lock failed: %d", ret);
        pthread_mutex_destroy(&ctx->sub_lock);
        pthread_mutex_destroy(&ctx->pool_lock);
        return ret;
    }

    ret = pthread_rwlock_init(&ctx->rwlock, NULL);
    if (ret != 0) {
        LOG_E("[BUS INIT] Init rwlock failed: %d", ret);
        pthread_mutex_destroy(&ctx->publish_lock);
        pthread_mutex_destroy(&ctx->sub_lock);
        pthread_mutex_destroy(&ctx->pool_lock);
        return ret;
    }

    return DATA_BUS_OK;
}

/**
 * @brief  Destroy all synchronization locks
 * @param  ctx  Bus context pointer
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
// Public APIs Implementation
// ==========================================================================

int data_bus_init(const data_bus_config_t *config) {
    if (!config || !config->name || strlen(config->name) >= BUS_NAME_MAX_LEN) {
        return DATA_BUS_ERR_PARAM;
    }

    // Check if bus instance already exists
    if (_data_bus_find_ctx(config->name)) {
        LOG_E("Data Bus[%s]: Already exists", config->name);
        return DATA_BUS_ERR_EXIST;
    }

    pthread_mutex_lock(&s_table_lock);

    // Find free instance slot
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
        return DATA_BUS_ERR_FULL;
    }

    // Allocate bus context
    data_bus_context_t *ctx = NULL;
    int ret = _data_bus_alloc_context(&ctx);
    if (ret != DATA_BUS_OK) {
        pthread_mutex_unlock(&s_table_lock);
        return ret;
    }

    // Load user configuration
    ctx->config = *config;
    ctx->max_items = config->max_items ? config->max_items : DATA_BUS_MAX_ITEMS_DEFAULT;
    ctx->max_item_size = config->max_item_size ? config->max_item_size : DATA_BUS_MAX_ITEM_SIZE_DEFAULT;
    ctx->max_item_size = ALIGN_UP(ctx->max_item_size);
    ctx->max_subscribers = config->max_subscribers ? config->max_subscribers : DATA_BUS_MAX_SUBSCRIBERS_DEFAULT;
    ctx->latest_item_held = NULL;

    // Initialize memory pool
    ret = _data_bus_init_memory_pool(ctx);
    if (ret != DATA_BUS_OK) {
        mem_free(ctx);
        pthread_mutex_unlock(&s_table_lock);
        return ret;
    }

    // Initialize synchronization locks
    ret = _data_bus_init_locks(ctx);
    if (ret != DATA_BUS_OK) {
        mem_free(ctx->memory_pool);
        mem_free(ctx->subscribers);
        mem_free(ctx->items);
        mem_free(ctx);
        pthread_mutex_unlock(&s_table_lock);
        return ret;
    }

    // Register bus instance to global table
    strncpy(s_bus_table[free_idx].name, config->name, BUS_NAME_MAX_LEN-1);
    s_bus_table[free_idx].name[BUS_NAME_MAX_LEN-1] = '\0';
    ctx->config.name = s_bus_table[free_idx].name;
    s_bus_table[free_idx].bus = ctx;
    s_bus_table[free_idx].used = true;

    pthread_mutex_unlock(&s_table_lock);

    LOG_I("Data Bus[%s]: Init success (max_items=%u, max_item_size=%zu, max_subscribers=%u)",
          config->name, ctx->max_items, ctx->max_item_size, ctx->max_subscribers);

    return DATA_BUS_OK;
}

int data_bus_alloc(const char *name,
                   data_type_t type,
                   size_t size,
                   const char *producer,
                   data_bus_item_handle_t *out_item) {
    // Parameter validity check
    if (!name || !out_item || type == DATA_TYPE_INVALID) {
        return DATA_BUS_ERR_PARAM;
    }

    // Find valid bus context
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // Check data size limit
    if (size > ctx->max_item_size) {
        LOG_E("[BUS ALLOC] Data size exceeds limit: %zu > %zu", size, ctx->max_item_size);
        return DATA_BUS_ERR_PARAM;
    }

    // Allocate free item from pool
    pthread_mutex_lock(&ctx->pool_lock);
    data_item_t *item = _data_bus_find_free_item(ctx);
    if (!item) {
        // Debug: print pool status when full
        LOG_E("[BUS ALLOC] Memory pool full! Bus: %s", name);
        for (uint32_t i = 0; i < ctx->max_items; i++) {
            data_item_t *it = &ctx->items[i];
            LOG_E("  Item[%u]: ref=%u, published=%d, in_use=%d",
                  i, atomic_load(&it->info.ref_count), it->published, it->in_use);
        }
        pthread_mutex_unlock(&ctx->pool_lock);
        return DATA_BUS_ERR_FULL;
    }

    // Initialize item metadata
    _data_bus_reset_item(item);
    item->info.type = type;
    item->info.data_size = size;
    item->info.timestamp = _data_bus_get_timestamp_us();
    item->info.producer = producer;
    atomic_init(&item->info.ref_count, 1);  // Producer initial reference
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

int data_bus_set_item_size(data_bus_item_handle_t item, size_t actual_size) {
    data_item_t *ditem = (data_item_t *)item;
    // Safety validation
    if (!ditem || ditem->magic != DATA_BUS_ITEM_MAGIC || ditem->published) {
        return DATA_BUS_ERR_PARAM;
    }
    // Size limit check
    if (actual_size > ditem->info.data_size) {
        return DATA_BUS_ERR_PARAM;
    }
    // Update valid data length
    ditem->info.data_size = actual_size;
    return DATA_BUS_OK;
}

int data_bus_push(const char *name, data_bus_item_handle_t item) {
    // Parameter check
    if (!name || !item) {
        return DATA_BUS_ERR_PARAM;
    }

    // Find valid bus context
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // Validate data item
    data_item_t *ditem = (data_item_t *)item;
    if (ditem->magic != DATA_BUS_ITEM_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    pthread_mutex_lock(&ctx->publish_lock);

    // State validation
    if (!ditem->in_use || ditem->published) {
        pthread_mutex_unlock(&ctx->publish_lock);
        LOG_E("[BUS PUSH] State error: in_use=%d, published=%d", ditem->in_use, ditem->published);
        return DATA_BUS_ERR_STATE;
    }

    // Mark item as published (read-only)
    ditem->published = true;

    // Push Mode: notify all subscribers
    _data_bus_notify_subscribers(ctx, ditem);

    // Pull Mode: update latest data item
    _data_bus_update_latest_item(ctx, ditem);

    pthread_mutex_unlock(&ctx->publish_lock);

    return DATA_BUS_OK;
}

// Push Mode Only: Atomic refcount increment
int data_bus_push_acquire(data_bus_item_handle_t item)
{
    data_item_t *ditem = (data_item_t *)item;
    if (!ditem || ditem->magic != DATA_BUS_ITEM_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // Atomic safe increment
    atomic_fetch_add(&ditem->info.ref_count, 1);
    LOG_D("[BUS PUSH] Ref +1 item=%p, ref=%u",
          ditem, atomic_load(&ditem->info.ref_count));

    return DATA_BUS_OK;
}

int data_bus_subscribe(const char *name, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_handle_t *out_sub) {
    // Parameter check
    if (!name || !cb || !out_sub) {
        return DATA_BUS_ERR_PARAM;
    }

    // Find valid bus context
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    pthread_mutex_lock(&ctx->sub_lock);

    // Find free subscriber slot
    for (uint32_t i = 0; i < ctx->max_subscribers; i++) {
        if (!ctx->subscribers[i].valid) {
            ctx->subscribers[i].type = type;
            ctx->subscribers[i].cb = cb;
            ctx->subscribers[i].user_data = user_data;
            ctx->subscribers[i].valid = true;
            *out_sub = (data_bus_subscription_handle_t)&ctx->subscribers[i];
            pthread_mutex_unlock(&ctx->sub_lock);
            LOG_I("[BUS SUB] Subscribe success: Bus=%s, Type=%s", name, data_type_to_str(type));
            return DATA_BUS_OK;
        }
    }

    pthread_mutex_unlock(&ctx->sub_lock);
    LOG_E("[BUS SUB] Subscriber table full: Bus=%s", name);
    return DATA_BUS_ERR_FULL;
}

int data_bus_unsubscribe(const char *name, data_bus_subscription_handle_t *sub) {
    // Parameter check
    if (!name || !sub || !*sub) {
        return DATA_BUS_ERR_PARAM;
    }

    // Find valid bus context
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    data_subscriber_t *s = (data_subscriber_t *)*sub;

    pthread_mutex_lock(&ctx->sub_lock);
    s->valid = false;
    pthread_mutex_unlock(&ctx->sub_lock);

    *sub = NULL;
    LOG_I("[BUS SUB] Unsubscribe success: Bus=%s", name);
    return DATA_BUS_OK;
}

int data_bus_pull_latest(const char *name,
                         data_type_t type,
                         data_bus_item_handle_t *out_item) {
    // Parameter check
    if (!name || !out_item) {
        return DATA_BUS_ERR_PARAM;
    }

    // Find valid bus context
    data_bus_context_t *ctx = _data_bus_find_ctx(name);
    if (!ctx || ctx->magic != DATA_BUS_MAGIC) {
        return DATA_BUS_ERR_MAGIC;
    }

    // Read lock for concurrent access
    pthread_rwlock_rdlock(&ctx->rwlock);

    if (!ctx->latest_item_held) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return DATA_BUS_ERR_NO_DATA;
    }

    data_item_t *item = ctx->latest_item_held;

    // Data type filter check
    if (type != DATA_TYPE_INVALID && item->info.type != type) {
        pthread_rwlock_unlock(&ctx->rwlock);
        return DATA_BUS_ERR_TYPE;
    }

    // Increment reference count
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

size_t data_bus_get_item_size(data_bus_item_handle_t item) {
    data_item_t *ditem = (data_item_t *)item;
    // Validity check
    if (!ditem || ditem->magic != DATA_BUS_ITEM_MAGIC) {
        return 0;
    }
    return ditem->info.data_size;
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

    // Write lock to prevent concurrent access
    pthread_rwlock_wrlock(&ctx->rwlock);

    // Release latest item reference
    if (ctx->latest_item_held) {
        _data_bus_release_item_safe(ctx->latest_item_held);
        ctx->latest_item_held = NULL;
    }

    // Force reset all items
    for (uint32_t i = 0; i < ctx->max_items; i++) {
        _data_bus_reset_item(&ctx->items[i]);
    }

    // Free all allocated memory
    mem_free(ctx->memory_pool);
    mem_free(ctx->items);
    mem_free(ctx->subscribers);

    pthread_rwlock_unlock(&ctx->rwlock);

    // Destroy synchronization locks
    _data_bus_destroy_locks(ctx);

    // Free bus context
    mem_free(ctx);

    // Clear global instance table
    pthread_mutex_lock(&s_table_lock);
    for (int i = 0; i < MAX_DATA_BUS; i++) {
        if (s_bus_table[i].used && strcmp(s_bus_table[i].name, name) == 0) {
            memset(&s_bus_table[i], 0, sizeof(data_bus_entry_t));
            break;
        }
    }
    pthread_mutex_unlock(&s_table_lock);

    LOG_I("Data Bus[%s]: Destroy success", name);
    return DATA_BUS_OK;
}

const char* data_type_to_str(data_type_t type) {
    return (type < DATA_TYPE_MAX && g_data_type_str[type]) ? g_data_type_str[type] : "UNKNOWN";
}