#ifndef __QUEUE_H
#define __QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ==========================================================================
// 配置宏：是否启用线程安全（Linux多线程环境下建议开启）
// ==========================================================================
#define QUEUE_ENABLE_THREAD_SAFE  1

#if QUEUE_ENABLE_THREAD_SAFE
#include <pthread.h>
#endif

#define QUEUE_STATIC_BUFFER_SIZE(type, count)  (sizeof(type*) * (count))
/**
 * @brief 辅助宏：定义队列静态缓冲区
 * @param name 缓冲区名称
 * @param type 队列元素类型（指针类型）
 * @param count 队列容量
 */
#define QUEUE_DEFINE_STATIC_BUFFER(name, type, count) \
    static type* name[count]

// ==========================================================================
// 队列句柄结构体（通用指针队列）
// ==========================================================================
typedef struct {
    void **buffer;          // 指向指针数组的指针（存储 void* 元素）
    uint32_t size;          // 队列总容量（注意：实际能存 size-1 个元素）
    uint32_t head;          // 头指针（写入位置）
    uint32_t tail;          // 尾指针（读取位置）

#if QUEUE_ENABLE_THREAD_SAFE
    pthread_mutex_t mutex;  // 互斥锁（保证多线程安全）
#endif
} Queue_t;

// ==========================================================================
// 错误码定义（可选，用于更精细的错误处理）
// ==========================================================================
typedef enum {
    QUEUE_OK = 0,
    QUEUE_ERR_FULL,         // 队列已满
    QUEUE_ERR_EMPTY,        // 队列为空
    QUEUE_ERR_NULL_PARAM,   // 参数为空
    QUEUE_ERR_LOCK          // 加锁失败（仅线程安全模式下）
} QueueErr_t;

// ==========================================================================
// 对外 API 接口
// ==========================================================================

/**
 * @brief 初始化队列
 * @param q        队列句柄指针
 * @param buffer   外部传入的指针数组缓冲区 (void* 数组)
 * @param size     缓冲区的总大小（元素个数，建议为 2 的幂次）
 * @note 【重要】实际可存储元素数量为 (size - 1)，这是环形队列的经典设计
 *       用于区分"空"和"满"两种状态。
 */
void Queue_Init(Queue_t *q, void **buffer, uint32_t size);

/**
 * @brief 放入一个元素（指针入队）
 * @param q    队列句柄指针
 * @param item 要存入的指针（可以是任意类型的对象指针）
 * @return 错误码
 */
QueueErr_t Queue_Put(Queue_t *q, void *item);

/**
 * @brief 取出一个元素（指针出队）
 * @param q    队列句柄指针
 * @param item 输出参数，用于存放取出的指针
 * @return 错误码
 */
QueueErr_t Queue_Get(Queue_t *q, void **item);

/**
 * @brief 查看队头元素但不出队（Peek操作）
 * @param q    队列句柄指针
 * @param item 输出参数，用于存放队头指针
 * @return 错误码
 */
QueueErr_t Queue_Peek(Queue_t *q, void **item);

/**
 * @brief 判断队列是否为空
 * @param q 队列句柄指针
 * @return true 为空
 */
bool Queue_IsEmpty(Queue_t *q);

/**
 * @brief 判断队列是否已满
 * @param q 队列句柄指针
 * @return true 为满
 */
bool Queue_IsFull(Queue_t *q);

/**
 * @brief 获取当前队列中元素的个数
 * @param q 队列句柄指针
 * @return 元素个数
 */
uint32_t Queue_GetCount(Queue_t *q);

/**
 * @brief 清空队列（重置指针，不释放内存）
 * @param q 队列句柄指针
 */
void Queue_Clear(Queue_t *q);

#endif /* __QUEUE_H */