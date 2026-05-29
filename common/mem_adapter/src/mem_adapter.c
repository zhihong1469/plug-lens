/* SPDX-License-Identifier: MIT */
/**
 * @file    mem_adapter.c
 * @brief   Implementation of dual-mode thread-safe memory adapter
 * @details Low-level implementation:
 *          1. TLSF (Two-Level Segregate Fit) high-performance static allocator
 *          2. Linux native libc malloc fallback for debugging
 *  3. pthread mutex for full thread safety
 *          4. Zero-cost compile-time mode switching
 *          5. Unified API layer for upper-level modules
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "mem_adapter.h"
#include <string.h>

// ==========================================================================
// Thread-safety Mutex (Linux Platform Implementation)
// ==========================================================================
#include <pthread.h>
static pthread_mutex_t g_mem_lock = PTHREAD_MUTEX_INITIALIZER;  /**< Global mutex for memory operations */

/** Lock memory allocator for exclusive access */
#define MEM_LOCK()      pthread_mutex_lock(&g_mem_lock)
/** Unlock memory allocator after operation */
#define MEM_UNLOCK()    pthread_mutex_unlock(&g_mem_lock)

// ==========================================================================
// Mode 1: USE_TLSF = 1 -> TLSF Static Memory Pool Implementation
// ==========================================================================
#if USE_TLSF
#include "tlsf.h"

static tlsf_t  g_tlsf = NULL;       /**< TLSF allocator handle */
static void*  g_mem_pool = NULL;   /**< Static memory pool base address */

/**
 * @brief   Initialize TLSF memory pool
 */
void mem_init(void *pool, size_t pool_size)
{
    if (pool == NULL || pool_size == 0)
    {
        return;
    }

    MEM_LOCK();
    g_mem_pool = pool;
    g_tlsf = tlsf_create_with_pool(pool, pool_size);
    MEM_UNLOCK();
}

/**
 * @brief   Destroy TLSF allocator and reset state
 */
void mem_destroy(void)
{
    MEM_LOCK();
    if (g_tlsf != NULL)
    {
        tlsf_destroy(g_tlsf);
        g_tlsf = NULL;
        g_mem_pool = NULL;
    }
    MEM_UNLOCK();
}

/**
 * @brief   TLSF-based memory allocation
 */
void *mem_alloc(size_t size)
{
    if (size == 0 || g_tlsf == NULL)
    {
        return NULL;
    }

    MEM_LOCK();
    void *ptr = tlsf_malloc(g_tlsf, size);
    MEM_UNLOCK();
    return ptr;
}

/**
 * @brief   TLSF-based zero-initialized allocation
 */
void *mem_calloc(size_t num, size_t size)
{
    const size_t total_size = num * size;
    if (total_size == 0 || g_tlsf == NULL)
    {
        return NULL;
    }

    MEM_LOCK();
    void *ptr = tlsf_malloc(g_tlsf, total_size);
    if (ptr != NULL)
    {
        memset(ptr, 0, total_size);
    }
    MEM_UNLOCK();
    return ptr;
}

/**
 * @brief   TLSF-based aligned memory allocation
 */
void *mem_memalign(size_t align, size_t size)
{
    if (align == 0 || size == 0 || g_tlsf == NULL)
    {
        return NULL;
    }

    MEM_LOCK();
    void *ptr = tlsf_memalign(g_tlsf, align, size);
    MEM_UNLOCK();
    return ptr;
}

/**
 * @brief   TLSF-based memory free
 */
void mem_free(void *ptr)
{
    if (ptr == NULL || g_tlsf == NULL)
    {
        return;
    }

    MEM_LOCK();
    tlsf_free(g_tlsf, ptr);
    MEM_UNLOCK();
}

// ==========================================================================
// Mode 2: USE_TLSF = 0 -> Linux Native Malloc Implementation (Debug Only)
// ==========================================================================
#else
#include <stdlib.h>

/**
 * @brief   Native mode initialization (no-op for compatibility)
 */
void mem_init(void *pool, size_t pool_size)
{
    // No initialization required for native libc allocator
    (void)pool;
    (void)pool_size;
}

/**
 * @brief   Native mode destroy (no-op for compatibility)
 */
void mem_destroy(void)
{
    // No resource release required for native libc allocator
}

/**
 * @brief   Wrapper for standard malloc
 */
void *mem_alloc(size_t size)
{
    MEM_LOCK();
    void *ptr = malloc(size);
    MEM_UNLOCK();
    return ptr;
}

/**
 * @brief   Wrapper for standard calloc
 */
void *mem_calloc(size_t num, size_t size)
{
    MEM_LOCK();
    void *ptr = calloc(num, size);
    MEM_UNLOCK();
    return ptr;
}

/**
 * @brief   Wrapper for Linux posix_memalign
 */
void *mem_memalign(size_t align, size_t size)
{
    void *ptr = NULL;
    MEM_LOCK();
    // Standard Linux aligned memory allocation
    const int ret = posix_memalign(&ptr, align, size);
    MEM_UNLOCK();
    return (ret == 0) ? ptr : NULL;
}

/**
 * @brief   Wrapper for standard free
 */
void mem_free(void *ptr)
{
    MEM_LOCK();
    free(ptr);
    MEM_UNLOCK();
}

#endif