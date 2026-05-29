/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           data_bus.h
 * @brief          High-Performance Zero-Copy Data Bus for Embedded Linux [V4.0 Standard]
 * @defgroup       DATA_BUS
 * @details
 * 【Core Features】
 *  1. Pure Zero-Copy Design: Only pass data pointers, no copy for large data (video/AI results)
 *  2. Dual Working Modes: Push Mode (active notify) + Pull Mode (active fetch)
 *  3. Multi-Instance Management: Support up to 4 independent buses (video/audio/AI)
 *  4. Pooled Memory: Pre-allocated TLSF static memory, eliminate fragmentation & leaks
 *  5. C11 Atomic Refcount: Lock-free high performance, safe underflow protection
 *
 * 【V4.0 Key Improvements】
 *  1. Clear API Semantics: Separate push/pull, eliminate ambiguity
 *  2. Safe Refcount: Add underflow protection, prevent illegal operations
 *  3. Single Responsibility: Split large functions, clean logic
 *  4. Optimized Lock Granularity: Reduce critical sections, improve concurrency
 *  5. Robust Error Handling: Full parameter check, unified error codes
 *
 * 【Core Design Rules】
 *  1. Push Mode: Consumer MUST call data_bus_push_acquire/data_bus_release
 *  2. Pull Mode: MUST pair data_bus_pull_latest/data_bus_release
 *  3. Memory Managed Exclusively: Forbid external malloc/free
 *  4. Thread-Safe: All public APIs support multi-thread calls
 * 
 * @example
 *  // Producer Example (Push/Pull Mode, same usage):
 *  1. Allocate frame        data_bus_alloc
 *  2. Fill data (camera/source)  void *w_buf = data_bus_get_writable_ptr(item);
 *  3. Push to bus (auto dual-mode)  data_bus_push
 *  4. Producer release ref  data_bus_release  
 *   // Consumer Example (Push Mode Callback):
 *  1. Subscribe bus         data_bus_subscribe
 *  2. Process data in cb    cb(item){1. Push Mode MUST: ref+1 if (data_bus_push_acquire(item) != DATA_BUS_OK) 2. Enqueue (NO heavy work in callback)}
 *  3. Worker thread: process → release ref  data_bus_release
 *   // Consumer Example (Pull Mode):
 *  1. Timer/thread fetch latest  data_bus_pull_latest
 *  2. Process → release ref      data_bus_release
 * @author         luo
 * @date           2026
 ******************************************************************************
 */

#ifndef DATA_BUS_H
#define DATA_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vision_ai_config.h"

// ==========================================================================
// Opaque Handles (Internal structure hidden, ensure encapsulation)
// ==========================================================================
typedef struct data_bus_item_t* data_bus_item_handle_t;
typedef struct data_bus_subscription_t* data_bus_subscription_handle_t;

// ==========================================================================
// Push Mode Callback Function
// ==========================================================================
typedef void (*data_bus_callback_t)(data_bus_item_handle_t item, void *user_data);

// ==========================================================================
// Bus Configuration Structure
// ==========================================================================
typedef struct {
    size_t max_item_size;       /**< Max size of single data (e.g. 640*360*2 for video) */
    uint32_t max_items;         /**< Max cached items (pool size, ≥8 recommended) */
    uint32_t max_subscribers;   /**< Max subscribers (e.g. LCD+AI+stream) */
    const char *name;          /**< Unique bus name (e.g. "video_bus") */
} data_bus_config_t;

// ==========================================================================
// Error Code Definition
// ==========================================================================
#define DATA_BUS_OK              0
#define DATA_BUS_ERR_PARAM      -1  /**< Invalid parameter */
#define DATA_BUS_ERR_EXIST      -2  /**< Bus already exists */
#define DATA_BUS_ERR_FULL       -3  /**< Instance/pool/subscriber table full */
#define DATA_BUS_ERR_MEM        -4  /**< Memory allocation failed */
#define DATA_BUS_ERR_MAGIC      -5  /**< Magic check failed (invalid handle) */
#define DATA_BUS_ERR_STATE      -6  /**< State error (duplicate publish/unpublished) */
#define DATA_BUS_ERR_NO_DATA    -7  /**< No data available for pull */
#define DATA_BUS_ERR_TYPE       -8  /**< Data type mismatch */
#define DATA_BUS_ERR_REF_UNDERFLOW -9  /**< Refcount underflow */

// ==========================================================================
// Public Core APIs
// ==========================================================================
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief  Initialize data bus
 * @param  config: Bus configuration parameters
 * @return DATA_BUS_OK on success, negative on failure
 * @note   1. Initialize mem_adapter (TLSF pool) BEFORE calling this API
 *         2. Same name bus can only be initialized once
 */
int data_bus_init(const data_bus_config_t *config);

/**
 * @brief  Producer allocates data memory
 * @param  name: Bus name
 * @param  type: Data type (VIDEO_FRAME/AI_RESULT)
 * @param  size: Actual data size
 * @param  producer: Producer ID (debug use)
 * @param  out_item: Output item handle
 * @return DATA_BUS_OK on success, negative on failure
 * @note   After alloc, MUST push OR release on exception
 */
int data_bus_alloc(const char *name,
                   data_type_t type,
                   size_t size,
                   const char *producer,
                   data_bus_item_handle_t *out_item);

/**
 * @brief  Get writable pointer (Producer Only)
 * @param  item: Data item handle
 * @return Writable memory address, NULL on failure
 * @note   Only call AFTER alloc AND BEFORE push
 */
void* data_bus_get_writable_ptr(data_bus_item_handle_t item);

/**
 * @brief  Set actual valid data size (after encode/fill)
 * @param  item: Data item handle
 * @param  actual_size: Actual valid data length
 * @return DATA_BUS_OK on success
 */
int data_bus_set_item_size(data_bus_item_handle_t item, size_t actual_size);

/**
 * @brief  Get actual valid data size (for JPEG dynamic length)
 * @param  item: Data item handle
 * @return Actual size on success, 0 on failure
 */
size_t data_bus_get_item_size(data_bus_item_handle_t item);

/**
 * @brief  Publish data (Universal for Push/Pull)
 * @param  name: Bus name
 * @param  item: Data item handle
 * @return DATA_BUS_OK on success, negative on failure
 * @note   1. Data becomes READ-ONLY after publish
 *         2. Push Mode: Auto notify all subscribers
 *         3. Pull Mode: Update latest item for consumers
 *         4. Producer MUST release initial ref after push
 */
int data_bus_push(const char *name, data_bus_item_handle_t item);

/**
 * @brief  Push Mode Only: Refcount +1
 * @param  item: Data item handle
 * @return DATA_BUS_OK on success, negative on failure
 */
int data_bus_push_acquire(data_bus_item_handle_t item);

/**
 * @brief  Subscribe data (Push Mode)
 * @param  name: Bus name
 * @param  type: Data type filter (DATA_TYPE_INVALID for all)
 * @param  cb: Data arrival callback
 * @param  user_data: User private data
 * @param  out_sub: Output subscription handle
 * @return DATA_BUS_OK on success, negative on failure
 * @note   MUST call data_bus_release in callback
 */
int data_bus_subscribe(const char *name, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_handle_t *out_sub);

/**
 * @brief  Unsubscribe data
 * @param  name: Bus name
 * @param  sub: Subscription handle
 * @return DATA_BUS_OK on success, negative on failure
 * @note   Set handle to NULL after call to avoid wild pointer
 */
int data_bus_unsubscribe(const char *name, data_bus_subscription_handle_t *sub);

/**
 * @brief  Pull Mode: Actively fetch latest data
 * @param  name: Bus name
 * @param  type: Data type filter
 * @param  out_item: Output data item handle
 * @return DATA_BUS_OK on success, negative on failure
 * @note   1. MUST pair with data_bus_release
 *         2. Thread-safe for concurrent pull
 *         3. Auto discard expired frames for real-time
 */
int data_bus_pull_latest(const char *name,
                         data_type_t type,
                         data_bus_item_handle_t *out_item);

/**
 * @brief  Get read-only pointer (Consumer Only)
 * @param  item: Data item handle
 * @return Read-only memory address, NULL on failure
 */
const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item);

/**
 * @brief  Release data item (Refcount -1, auto recycle at 0)
 * @param  item: Data item handle
 * @return DATA_BUS_OK on success, negative on failure
 *
 * @details 【Core Usage · MUST READ】
 *  1. 【Push Mode Callback】→ Manual call in user callback
 *  2. 【Pull Mode Usage】  → Mandatory call, paired with pull
 *  3. 【Producer Exception】→ Release if alloc without push
 *  4. 【After Publish】    → Mandatory call, release producer ref
 *
 * @note   Thread-safe, atomic operation, lock-free, underflow protected
 */
int data_bus_release(data_bus_item_handle_t item);

// -------------------------------------------------------------------------
// Bus Management APIs
// -------------------------------------------------------------------------
/**
 * @brief  Destroy bus and release all resources
 * @param  name: Bus name
 * @return DATA_BUS_OK on success, negative on failure
 */
int data_bus_deinit(const char *name);

/**
 * @brief  Convert data type to string (debug log only)
 * @param  type: Data type enum
 * @return Type string
 */
const char* data_type_to_str(data_type_t type);

#ifdef __cplusplus
}
#endif
#endif /* DATA_BUS_H */