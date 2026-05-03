#ifndef __POOL_H
#define __POOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ==========================================================================
// 配置宏：是否启用线程安全
// ==========================================================================
#define POOL_ENABLE_THREAD_SAFE  1

#if POOL_ENABLE_THREAD_SAFE
#include <pthread.h>
#endif

// ==========================================================================
// 错误码定义
// ==========================================================================
typedef enum {
    POOL_OK = 0,
    POOL_ERR_FULL,         // 池已满
    POOL_ERR_EMPTY,        // 池已空
    POOL_ERR_NULL_PARAM,   // 参数为空
    POOL_ERR_LOCK,         // 加锁失败
    POOL_ERR_NO_MEMORY,    // 内存不足
    POOL_ERR_INVALID       // 无效操作
} PoolErr_t;

// ==========================================================================
// 对象池句柄结构体（通用对象池）
// ==========================================================================
typedef struct {
    void *memory_pool;          // 指向预分配的内存池
    void **free_list;           // 空闲对象指针列表（栈结构）
    uint32_t item_size;         // 单个对象的大小（字节）
    uint32_t pool_capacity;     // 池的总容量
    uint32_t free_count;        // 当前空闲对象数量
    uint32_t top;               // 空闲列表栈顶指针

#if POOL_ENABLE_THREAD_SAFE
    pthread_mutex_t mutex;      // 互斥锁（保证多线程安全）
#endif
} Pool_t;

// ==========================================================================
// 对外 API 接口
// ==========================================================================

/**
 * @brief 初始化对象池（静态内存模式，外部传入缓冲区）
 * @param pool           对象池句柄指针
 * @param item_size      单个对象的大小（字节）
 * @param pool_capacity  池的总容量（对象个数）
 * @param memory_buffer  外部传入的内存缓冲区（大小至少为 item_size * pool_capacity）
 * @param free_list_buffer  外部传入的空闲列表缓冲区（大小至少为 sizeof(void*) * pool_capacity）
 * @note 【静态内存模式】所有内存由外部管理，避免运行时 malloc 碎片
 */
void Pool_Init(Pool_t *pool,
               size_t item_size,
               uint32_t pool_capacity,
               void *memory_buffer,
               void **free_list_buffer);

/**
 * @brief 从池中获取一个对象
 * @param pool  对象池句柄指针
 * @param out_item  输出参数，返回对象指针
 * @return 错误码
 */
PoolErr_t Pool_Acquire(Pool_t *pool, void **out_item);

/**
 * @brief 归还一个对象到池中
 * @param pool  对象池句柄指针
 * @param item  要归还的对象指针
 * @return 错误码
 */
PoolErr_t Pool_Release(Pool_t *pool, void *item);

/**
 * @brief 判断池是否为空
 * @param pool  对象池句柄指针
 * @return true 为空
 */
bool Pool_IsEmpty(Pool_t *pool);

/**
 * @brief 判断池是否已满
 * @param pool  对象池句柄指针
 * @return true 为满
 */
bool Pool_IsFull(Pool_t *pool);

/**
 * @brief 获取当前空闲对象数量
 * @param pool  对象池句柄指针
 * @return 空闲对象数量
 */
uint32_t Pool_GetFreeCount(Pool_t *pool);

/**
 * @brief 重置池（清空所有对象，不释放内存）
 * @param pool  对象池句柄指针
 */
void Pool_Reset(Pool_t *pool);

#endif /* __POOL_H */