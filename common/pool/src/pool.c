#include "pool.h"
#include <string.h>

// ==========================================================================
// 内部辅助宏（仅线程安全模式使用）
// ==========================================================================
#if POOL_ENABLE_THREAD_SAFE
    #define POOL_LOCK(pool)    pthread_mutex_lock(&(pool)->mutex)
    #define POOL_UNLOCK(pool)  pthread_mutex_unlock(&(pool)->mutex)
#else
    #define POOL_LOCK(pool)    ((void)0)
    #define POOL_UNLOCK(pool)  ((void)0)
#endif

// ==========================================================================
// API 实现
// ==========================================================================

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

    // 初始化基本参数
    pool->item_size = item_size;
    pool->pool_capacity = pool_capacity;
    pool->memory_pool = memory_buffer;
    pool->free_list = free_list_buffer;
    pool->free_count = pool_capacity;
    pool->top = 0;

    // 初始化空闲列表（栈结构）
    // 将所有对象指针压入空闲列表
    for (uint32_t i = 0; i < pool_capacity; i++) {
        // 计算第 i 个对象的地址
        pool->free_list[i] = (uint8_t*)memory_buffer + (i * item_size);
    }
    pool->top = pool_capacity; // 栈顶指向最后一个元素的下一个位置

    // 清零内存（可选，根据需求）
    memset(memory_buffer, 0, item_size * pool_capacity);

#if POOL_ENABLE_THREAD_SAFE
    pthread_mutex_init(&pool->mutex, NULL);
#endif
}

PoolErr_t Pool_Acquire(Pool_t *pool, void **out_item)
{
    if (pool == NULL || out_item == NULL) {
        return POOL_ERR_NULL_PARAM;
    }

    POOL_LOCK(pool);

    // 检查池是否为空
    if (pool->top == 0) {
        POOL_UNLOCK(pool);
        return POOL_ERR_EMPTY;
    }

    // 从栈顶弹出一个对象
    pool->top--;
    *out_item = pool->free_list[pool->top];
    pool->free_count--;

    POOL_UNLOCK(pool);
    return POOL_OK;
}

PoolErr_t Pool_Release(Pool_t *pool, void *item)
{
    if (pool == NULL || item == NULL) {
        return POOL_ERR_NULL_PARAM;
    }

    POOL_LOCK(pool);

    // 检查池是否已满
    if (pool->top >= pool->pool_capacity) {
        POOL_UNLOCK(pool);
        return POOL_ERR_FULL;
    }

    // 检查指针是否在池范围内（安全检查）
    uintptr_t item_addr = (uintptr_t)item;
    uintptr_t pool_start = (uintptr_t)pool->memory_pool;
    uintptr_t pool_end = pool_start + (pool->pool_capacity * pool->item_size);
    
    if (item_addr < pool_start || item_addr >= pool_end) {
        POOL_UNLOCK(pool);
        return POOL_ERR_INVALID;
    }

    // 将对象压入空闲列表栈
    pool->free_list[pool->top] = item;
    pool->top++;
    pool->free_count++;

    POOL_UNLOCK(pool);
    return POOL_OK;
}

bool Pool_IsEmpty(Pool_t *pool)
{
    if (pool == NULL) return true;

    bool empty;
    POOL_LOCK(pool);
    empty = (pool->top == 0);
    POOL_UNLOCK(pool);
    return empty;
}

bool Pool_IsFull(Pool_t *pool)
{
    if (pool == NULL) return true;

    bool full;
    POOL_LOCK(pool);
    full = (pool->top >= pool->pool_capacity);
    POOL_UNLOCK(pool);
    return full;
}

uint32_t Pool_GetFreeCount(Pool_t *pool)
{
    if (pool == NULL) return 0;

    uint32_t count;
    POOL_LOCK(pool);
    count = pool->free_count;
    POOL_UNLOCK(pool);
    return count;
}

void Pool_Reset(Pool_t *pool)
{
    if (pool == NULL) return;

    POOL_LOCK(pool);
    
    // 重置空闲列表
    for (uint32_t i = 0; i < pool->pool_capacity; i++) {
        pool->free_list[i] = (uint8_t*)pool->memory_pool + (i * pool->item_size);
    }
    pool->top = pool->pool_capacity;
    pool->free_count = pool->pool_capacity;

    // 清零内存
    memset(pool->memory_pool, 0, pool->item_size * pool->pool_capacity);

    POOL_UNLOCK(pool);
}