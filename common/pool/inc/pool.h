/* SPDX-License-Identifier: MIT */
/**
 * @file    pool.h
 * @brief   Universal static object pool for embedded Vision AI system
 * @details Core features:
 *          1. Static memory mode (external buffer, no runtime fragmentation)
 *          2. Stack-based free list for high-performance object management
 *          3. Optional thread-safe mutex protection
 *          4. Zero dynamic memory allocation, embedded-optimized
 *          5. Standard error code system for robust operation
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. Use static external buffers to avoid heap fragmentation
 *          2. Thread safety controlled by compile-time macro
 *          3. All APIs are thread-safe when mutex is enabled
 */
#ifndef __POOL_H
#define __POOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// Configuration Macro: Thread Safety Enable
// ==========================================================================
/**
 * @brief   Thread safety configuration switch
 * @details 1 = Enable pthread mutex for multi-thread access
 *          0 = Disable mutex for maximum single-thread performance
 */
#define POOL_ENABLE_THREAD_SAFE  1

#if POOL_ENABLE_THREAD_SAFE
#include <pthread.h>
#endif

// ==========================================================================
// Object Pool Error Code Definition
// ==========================================================================
/**
 * @brief   Object pool operation status error codes
 */
typedef enum {
    POOL_OK = 0,                /**< Operation successful */
    POOL_ERR_FULL,              /**< Object pool is full */
    POOL_ERR_EMPTY,             /**< Object pool is empty */
    POOL_ERR_NULL_PARAM,        /**< NULL input parameter */
    POOL_ERR_LOCK,              /**< Mutex lock/unlock failed */
    POOL_ERR_NO_MEMORY,         /**< Insufficient memory */
    POOL_ERR_INVALID            /**< Invalid operation */
} PoolErr_t;

// ==========================================================================
// Universal Object Pool Handle Structure
// ==========================================================================
/**
 * @brief   Object pool handle (static memory based)
 * @details Manages fixed-size objects with stack-style free list
 * @note    All memory managed externally, no internal allocation
 */
typedef struct {
    void *memory_pool;          /**< Base address of pre-allocated memory buffer */
    void **free_list;           /**< Free object pointer list (stack structure) */
    uint32_t item_size;         /**< Size of single object in bytes */
    uint32_t pool_capacity;     /**< Maximum number of objects (total capacity) */
    uint32_t free_count;        /**< Current number of free available objects */
    uint32_t top;               /**< Stack top pointer of free list */

#if POOL_ENABLE_THREAD_SAFE
    pthread_mutex_t mutex;      /**< Thread safety mutex for concurrent access */
#endif
} Pool_t;

// ==========================================================================
// Public API Interface
// ==========================================================================

/**
 * @brief   Initialize object pool (static memory mode)
 * @param   pool            Pointer to object pool handle
 * @param   item_size       Size of one single object (bytes)
 * @param   pool_capacity   Total capacity (number of objects)
 * @param   memory_buffer   External memory buffer (size >= item_size * capacity)
 * @param   free_list_buffer External free list buffer (size >= sizeof(void*) * capacity)
 * @return  None
 *
 * @note    Static memory mode: All buffers managed externally
 *          Eliminates runtime malloc and memory fragmentation
 * @pre     All input buffers must be valid and sufficiently sized
 * @thread_safety No
 */
void Pool_Init(Pool_t *pool,
               size_t item_size,
               uint32_t pool_capacity,
               void *memory_buffer,
               void **free_list_buffer);

/**
 * @brief   Acquire one free object from the pool
 * @param   pool        Pointer to object pool handle
 * @param   out_item    Output pointer to the acquired object
 * @return  PoolErr_t   Error code (POOL_OK on success)
 *
 * @thread_safety Yes (if enabled)
 */
PoolErr_t Pool_Acquire(Pool_t *pool, void **out_item);

/**
 * @brief   Return one object back to the pool
 * @param   pool        Pointer to object pool handle
 * @param   item        Pointer to the object to release
 * @return  PoolErr_t   Error code (POOL_OK on success)
 *
 * @thread_safety Yes (if enabled)
 */
PoolErr_t Pool_Release(Pool_t *pool, void *item);

/**
 * @brief   Check if the object pool is empty
 * @param   pool    Pointer to object pool handle
 * @return  true    Pool is empty
 *
 * @thread_safety Yes (if enabled)
 */
bool Pool_IsEmpty(Pool_t *pool);

/**
 * @brief   Check if the object pool is full
 * @param   pool    Pointer to object pool handle
 * @return  true    Pool is full
 *
 * @thread_safety Yes (if enabled)
 */
bool Pool_IsFull(Pool_t *pool);

/**
 * @brief   Get current number of free objects
 * @param   pool        Pointer to object pool handle
 * @return  uint32_t    Count of free available objects
 *
 * @thread_safety Yes (if enabled)
 */
uint32_t Pool_GetFreeCount(Pool_t *pool);

/**
 * @brief   Reset object pool (clear all objects, no memory free)
 * @param   pool    Pointer to object pool handle
 *
 * @post    Free list restored to full capacity
 * @thread_safety Yes (if enabled)
 */
void Pool_Reset(Pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* __POOL_H */