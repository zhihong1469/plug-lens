/* SPDX-License-Identifier: MIT */
/**
 * @file    pool.c
 * @brief   High-performance static object pool implementation
 * @details Core implementation features:
 *          1. Stack-based free list for O(1) alloc/free
 *          2. Architecture-aware automatic memory alignment
 *          3. Optional thread-safe mutex protection
 *          4. Static memory only (no runtime fragmentation)
 *          5. Pointer validity check for safety
 *          6. Zero-initialized memory on init/reset
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "pool.h"
#include <string.h>

// ==========================================================================
// Internal Helper Macros (Thread Safety)
// ==========================================================================
#if POOL_ENABLE_THREAD_SAFE
    /** Lock object pool for exclusive access */
    #define POOL_LOCK(pool)    pthread_mutex_lock(&(pool)->mutex)
    /** Unlock object pool after operation */
    #define POOL_UNLOCK(pool)  pthread_mutex_unlock(&(pool)->mutex)
#else
    /** No-op lock when thread safety is disabled */
    #define POOL_LOCK(pool)    ((void)0)
    /** No-op unlock when thread safety is disabled */
    #define POOL_UNLOCK(pool)  ((void)0)
#endif

// ==========================================================================
// Architecture-Aware Memory Alignment Macros
// ==========================================================================
#ifdef __x86_64__
#define MEM_ALIGN_MASK  7  /**< 64-bit system: 8-byte alignment */
#else
#define MEM_ALIGN_MASK  3  /**< 32-bit system: 4-byte alignment */
#endif
/** Align size up to architecture required boundary */
#define ALIGN_UP(size)   (((size) + MEM_ALIGN_MASK) & ~MEM_ALIGN_MASK)

// ==========================================================================
// Public API Implementation
// ==========================================================================

/**
 * @brief   Initialize static object pool
 * @param   pool            Pool handle pointer
 * @param   item_size       Raw size of single object
 * @param   pool_capacity   Total object count
 * @param   memory_buffer   External data buffer
 * @param   free_list_buffer External pointer list buffer
 * @note    Auto-alignment, zero-initialized memory, free list pre-built
 */
void Pool_Init(Pool_t *pool,
               size_t item_size,
               uint32_t pool_capacity,
               void *memory_buffer,
               void **free_list_buffer)
{
    if (pool == NULL || memory_buffer == NULL || free_list_buffer == NULL || 
        item_size == 0 || pool_capacity == 0) {
        return;
    }

    // Initialize core pool parameters with alignment
    pool->item_size = ALIGN_UP(item_size);
    pool->pool_capacity = pool_capacity;
    pool->memory_pool = memory_buffer;
    pool->free_list = free_list_buffer;
    pool->free_count = pool_capacity;
    pool->top = 0;

    // Pre-build free list (stack structure)
    for (uint32_t i = 0; i < pool_capacity; i++) {
        pool->free_list[i] = (uint8_t*)memory_buffer + (i * pool->item_size);
    }
    pool->top = pool_capacity;

    // Zero-initialize all object memory
    memset(memory_buffer, 0, pool->item_size * pool_capacity);

#if POOL_ENABLE_THREAD_SAFE
    pthread_mutex_init(&pool->mutex, NULL);
#endif
}

/**
 * @brief   Acquire one object from pool
 * @param   pool        Pool handle
 * @param   out_item    Output object pointer
 * @return  Error code
 * @note    O(1) stack pop operation, thread-safe
 */
PoolErr_t Pool_Acquire(Pool_t *pool, void **out_item)
{
    if (pool == NULL || out_item == NULL) {
        return POOL_ERR_NULL_PARAM;
    }

    POOL_LOCK(pool);

    if (pool->top == 0) {
        POOL_UNLOCK(pool);
        return POOL_ERR_EMPTY;
    }

    // Pop from stack top
    pool->top--;
    *out_item = pool->free_list[pool->top];
    pool->free_count--;

    POOL_UNLOCK(pool);
    return POOL_OK;
}

/**
 * @brief   Release object back to pool
 * @param   pool    Pool handle
 * @param   item    Object pointer to release
 * @return  Error code
 * @note    O(1) stack push, pointer range validation for safety
 * @note    Thread-safe
 */
PoolErr_t Pool_Release(Pool_t *pool, void *item)
{
    if (pool == NULL || item == NULL) {
        return POOL_ERR_NULL_PARAM;
    }

    POOL_LOCK(pool);

    if (pool->top >= pool->pool_capacity) {
        POOL_UNLOCK(pool);
        return POOL_ERR_FULL;
    }

    // Safety check: verify pointer belongs to this pool
    uintptr_t item_addr = (uintptr_t)item;
    uintptr_t pool_start = (uintptr_t)pool->memory_pool;
    uintptr_t pool_end = pool_start + (pool->pool_capacity * pool->item_size);
    
    if (item_addr < pool_start || item_addr >= pool_end) {
        POOL_UNLOCK(pool);
        return POOL_ERR_INVALID;
    }

    // Push to stack top
    pool->free_list[pool->top] = item;
    pool->top++;
    pool->free_count++;

    POOL_UNLOCK(pool);
    return POOL_OK;
}

/**
 * @brief   Check if pool is empty
 * @param   pool    Pool handle
 * @return  true = empty
 * @note    Thread-safe
 */
bool Pool_IsEmpty(Pool_t *pool)
{
    if (pool == NULL) return true;

    bool empty;
    POOL_LOCK(pool);
    empty = (pool->top == 0);
    POOL_UNLOCK(pool);
    return empty;
}

/**
 * @brief   Check if pool is full
 * @param   pool    Pool handle
 * @return  true = full
 * @note    Thread-safe
 */
bool Pool_IsFull(Pool_t *pool)
{
    if (pool == NULL) return true;

    bool full;
    POOL_LOCK(pool);
    full = (pool->top >= pool->pool_capacity);
    POOL_UNLOCK(pool);
    return full;
}

/**
 * @brief   Get current free object count
 * @param   pool    Pool handle
 * @return  Free object count
 * @note    Thread-safe
 */
uint32_t Pool_GetFreeCount(Pool_t *pool)
{
    if (pool == NULL) return 0;

    uint32_t count;
    POOL_LOCK(pool);
    count = pool->free_count;
    POOL_UNLOCK(pool);
    return count;
}

/**
 * @brief   Reset pool to initial state
 * @param   pool    Pool handle
 * @note    Rebuild free list, zero memory, thread-safe
 */
void Pool_Reset(Pool_t *pool)
{
    if (pool == NULL) return;

    POOL_LOCK(pool);
    
    // Rebuild free list
    for (uint32_t i = 0; i < pool->pool_capacity; i++) {
        pool->free_list[i] = (uint8_t*)pool->memory_pool + (i * pool->item_size);
    }
    pool->top = pool->pool_capacity;
    pool->free_count = pool->pool_capacity;

    // Zero-initialize memory
    memset(pool->memory_pool, 0, pool->item_size * pool->pool_capacity);

    POOL_UNLOCK(pool);
}