/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           mem_adapter.c
 * @brief          内存适配层实现（TLSF/原生双模式）
 * @details        线程安全封装，屏蔽底层内存分配器差异，向上提供统一接口
 ******************************************************************************
 */
#include "mem_adapter.h"
#include <string.h>

// ==========================================================================
// 线程安全互斥锁（Linux平台实现）
// ==========================================================================
#include <pthread.h>
static pthread_mutex_t g_mem_lock = PTHREAD_MUTEX_INITIALIZER;

#define MEM_LOCK()      pthread_mutex_lock(&g_mem_lock)
#define MEM_UNLOCK()    pthread_mutex_unlock(&g_mem_lock)

// ==========================================================================
// 模式一：USE_TLSF = 1 -> TLSF 静态内存池实现
// ==========================================================================
#if USE_TLSF
#include "tlsf.h"

static tlsf_t      g_tlsf = NULL;
static void*      g_mem_pool = NULL;

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
// 模式二：USE_TLSF = 0 -> Linux 原生 malloc/free 实现（调试用）
// ==========================================================================
#else
#include <stdlib.h>

void mem_init(void *pool, size_t pool_size)
{
    // 原生模式无需初始化，兼容接口
    (void)pool;
    (void)pool_size;
}

void mem_destroy(void)
{
    // 原生模式无需销毁
}

void *mem_alloc(size_t size)
{
    MEM_LOCK();
    void *ptr = malloc(size);
    MEM_UNLOCK();
    return ptr;
}

void *mem_calloc(size_t num, size_t size)
{
    MEM_LOCK();
    void *ptr = calloc(num, size);
    MEM_UNLOCK();
    return ptr;
}

void *mem_memalign(size_t align, size_t size)
{
    void *ptr = NULL;
    MEM_LOCK();
    // Linux标准内存对齐分配
    const int ret = posix_memalign(&ptr, align, size);
    MEM_UNLOCK();
    return (ret == 0) ? ptr : NULL;
}

void mem_free(void *ptr)
{
    MEM_LOCK();
    free(ptr);
    MEM_UNLOCK();
}

#endif