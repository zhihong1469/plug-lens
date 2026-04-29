#include "queue.h"
#include <string.h>

// ==========================================================================
// 内部辅助宏（仅线程安全模式使用）
// ==========================================================================
#if QUEUE_ENABLE_THREAD_SAFE
    #define QUEUE_LOCK(q)    pthread_mutex_lock(&(q)->mutex)
    #define QUEUE_UNLOCK(q)  pthread_mutex_unlock(&(q)->mutex)
#else
    #define QUEUE_LOCK(q)    ((void)0)
    #define QUEUE_UNLOCK(q)  ((void)0)
#endif

// ==========================================================================
// API 实现
// ==========================================================================

void Queue_Init(Queue_t *q, void **buffer, uint32_t size)
{
    if (q == NULL || buffer == NULL || size == 0) {
        return;
    }

    q->buffer = buffer;
    q->size = size;
    q->head = 0;
    q->tail = 0;

#if QUEUE_ENABLE_THREAD_SAFE
    pthread_mutex_init(&q->mutex, NULL);
#endif
}

QueueErr_t Queue_Put(Queue_t *q, void *item)
{
    if (q == NULL) {
        return QUEUE_ERR_NULL_PARAM;
    }

    QUEUE_LOCK(q);

    // 检查队列是否已满 (经典环形队列判满逻辑：牺牲一个位置)
    uint32_t next_head = (q->head + 1) % q->size;
    if (next_head == q->tail) {
        QUEUE_UNLOCK(q);
        return QUEUE_ERR_FULL;
    }

    // 存入指针
    q->buffer[q->head] = item;
    q->head = next_head;

    QUEUE_UNLOCK(q);
    return QUEUE_OK;
}

QueueErr_t Queue_Get(Queue_t *q, void **item)
{
    if (q == NULL || item == NULL) {
        return QUEUE_ERR_NULL_PARAM;
    }

    QUEUE_LOCK(q);

    // 检查队列是否为空
    if (q->head == q->tail) {
        QUEUE_UNLOCK(q);
        return QUEUE_ERR_EMPTY;
    }

    // 取出指针
    *item = q->buffer[q->tail];
    q->tail = (q->tail + 1) % q->size;

    QUEUE_UNLOCK(q);
    return QUEUE_OK;
}

QueueErr_t Queue_Peek(Queue_t *q, void **item)
{
    if (q == NULL || item == NULL) {
        return QUEUE_ERR_NULL_PARAM;
    }

    QUEUE_LOCK(q);

    if (q->head == q->tail) {
        QUEUE_UNLOCK(q);
        return QUEUE_ERR_EMPTY;
    }

    // 仅查看，不移动尾指针
    *item = q->buffer[q->tail];

    QUEUE_UNLOCK(q);
    return QUEUE_OK;
}

bool Queue_IsEmpty(Queue_t *q)
{
    if (q == NULL) return true;

    bool empty;
    QUEUE_LOCK(q);
    empty = (q->head == q->tail);
    QUEUE_UNLOCK(q);
    return empty;
}

bool Queue_IsFull(Queue_t *q)
{
    if (q == NULL) return true;

    bool full;
    QUEUE_LOCK(q);
    uint32_t next_head = (q->head + 1) % q->size;
    full = (next_head == q->tail);
    QUEUE_UNLOCK(q);
    return full;
}

uint32_t Queue_GetCount(Queue_t *q)
{
    if (q == NULL) return 0;

    uint32_t count;
    QUEUE_LOCK(q);
    
    if (q->head >= q->tail) {
        count = q->head - q->tail;
    } else {
        count = q->size - (q->tail - q->head);
    }
    
    QUEUE_UNLOCK(q);
    return count;
}

void Queue_Clear(Queue_t *q)
{
    if (q == NULL) return;

    QUEUE_LOCK(q);
    q->head = 0;
    q->tail = 0;
    QUEUE_UNLOCK(q);
}