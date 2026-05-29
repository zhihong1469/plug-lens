/* SPDX-License-Identifier: MIT */
/**
 * @file    queue.c
 * @brief   High-performance circular queue implementation
 * @details Core implementation:
 *          1. Classic ring buffer algorithm (head/tail pointers)
 *          2. One dummy slot to resolve empty/full ambiguity
 *          3. Thread-safe mutex wrapper
 *          4. O(1) time complexity for all operations
 *          5. Static memory only, embedded-optimized
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "queue.h"
#include <string.h>

// ==========================================================================
// Internal Thread Safety Helper Macros
// ==========================================================================
#if QUEUE_ENABLE_THREAD_SAFE
    /** Lock queue for exclusive multi-thread access */
    #define QUEUE_LOCK(q)    pthread_mutex_lock(&(q)->mutex)
    /** Unlock queue after operation */
    #define QUEUE_UNLOCK(q)  pthread_mutex_unlock(&(q)->mutex)
#else
    /** No-operation lock (thread safety disabled) */
    #define QUEUE_LOCK(q)    ((void)0)
    /** No-operation unlock (thread safety disabled) */
    #define QUEUE_UNLOCK(q)  ((void)0)
#endif

// ==========================================================================
// Public API Implementation
// ==========================================================================

/**
 * @brief   Initialize queue structure and thread mutex
 */
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

/**
 * @brief   Add element to queue (enqueue)
 * @details Full check: (head + 1) % size == tail
 */
QueueErr_t Queue_Put(Queue_t *q, void *item)
{
    if (q == NULL) {
        return QUEUE_ERR_NULL_PARAM;
    }

    QUEUE_LOCK(q);

    // Classic circular queue full detection logic
    uint32_t next_head = (q->head + 1) % q->size;
    if (next_head == q->tail) {
        QUEUE_UNLOCK(q);
        return QUEUE_ERR_FULL;
    }

    // Write element and advance head pointer
    q->buffer[q->head] = item;
    q->head = next_head;

    QUEUE_UNLOCK(q);
    return QUEUE_OK;
}

/**
 * @brief   Remove element from queue (dequeue)
 * @details Empty check: head == tail
 */
QueueErr_t Queue_Get(Queue_t *q, void **item)
{
    if (q == NULL || item == NULL) {
        return QUEUE_ERR_NULL_PARAM;
    }

    QUEUE_LOCK(q);

    // Check empty state
    if (q->head == q->tail) {
        QUEUE_UNLOCK(q);
        return QUEUE_ERR_EMPTY;
    }

    // Read element and advance tail pointer
    *item = q->buffer[q->tail];
    q->tail = (q->tail + 1) % q->size;

    QUEUE_UNLOCK(q);
    return QUEUE_OK;
}

/**
 * @brief   Read head element without removing it
 */
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

    // Read only, no pointer movement
    *item = q->buffer[q->tail];

    QUEUE_UNLOCK(q);
    return QUEUE_OK;
}

/**
 * @brief   Check empty queue status
 */
bool Queue_IsEmpty(Queue_t *q)
{
    if (q == NULL) return true;

    bool empty;
    QUEUE_LOCK(q);
    empty = (q->head == q->tail);
    QUEUE_UNLOCK(q);
    return empty;
}

/**
 * @brief   Check full queue status
 */
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

/**
 * @brief   Calculate current number of elements in queue
 */
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

/**
 * @brief   Reset queue to empty state
 */
void Queue_Clear(Queue_t *q)
{
    if (q == NULL) return;

    QUEUE_LOCK(q);
    q->head = 0;
    q->tail = 0;
    QUEUE_UNLOCK(q);
}