/* SPDX-License-Identifier: MIT */
/**
 * @file    queue.h
 * @brief   Universal thread-safe static circular queue (pointer-based)
 * @details Core features for plug-lens Vision AI terminal:
 *          1. Static buffer only (no dynamic memory allocation)
 *          2. Classic circular queue design (size-1 capacity for state detection)
 *          3. Optional pthread mutex for multi-thread safety
 *          4. Generic void* element support for any object type
 *          5. Lightweight, high-performance O(1) enqueue/dequeue
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. Actual usable capacity = buffer size - 1
 *          2. Buffer size recommended to be power of 2 for performance
 *          3. All APIs are thread-safe when mutex is enabled
 */
#ifndef __QUEUE_H
#define __QUEUE_H

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
 * @brief   Thread safety switch for multi-thread environment
 * @details 1 = Enable pthread mutex (recommended for Linux)
 *          0 = Disable mutex for maximum single-thread performance
 */
#define QUEUE_ENABLE_THREAD_SAFE  1

#if QUEUE_ENABLE_THREAD_SAFE
#include <pthread.h>
#endif

/**
 * @brief   Calculate static buffer size for queue
 * @param   type    Element data type
 * @param   count   Maximum queue capacity
 * @return  Required buffer size in bytes
 */
#define QUEUE_STATIC_BUFFER_SIZE(type, count)  (sizeof(type*) * (count))

/**
 * @brief   Helper macro: Define static queue buffer
 * @param   name    Buffer variable name
 * @param   type    Element type (pointer type)
 * @param   count   Queue capacity
 * @note    Used for compile-time static buffer allocation
 */
#define QUEUE_DEFINE_STATIC_BUFFER(name, type, count) \
    static type* name[count]

// ==========================================================================
// Universal Circular Queue Handle Structure
// ==========================================================================
/**
 * @brief   Generic pointer-based circular queue handle
 * @details Manages void* elements with ring buffer architecture
 * @note    All memory managed externally, no internal allocation
 */
typedef struct {
    void **buffer;          /**< Pointer array buffer (stores void* elements) */
    uint32_t size;          /**< Total buffer size (usable = size - 1) */
    uint32_t head;          /**< Head pointer (write/enqueue position) */
    uint32_t tail;          /**< Tail pointer (read/dequeue position) */

#if QUEUE_ENABLE_THREAD_SAFE
    pthread_mutex_t mutex;  /**< Thread safety mutex for concurrent access */
#endif
} Queue_t;

// ==========================================================================
// Queue Operation Error Codes
// ==========================================================================
/**
 * @brief   Error codes for queue operations
 */
typedef enum {
    QUEUE_OK = 0,                /**< Operation successful */
    QUEUE_ERR_FULL,              /**< Queue is full (enqueue failed) */
    QUEUE_ERR_EMPTY,             /**< Queue is empty (dequeue failed) */
    QUEUE_ERR_NULL_PARAM,        /**< NULL input parameter */
    QUEUE_ERR_LOCK               /**< Mutex lock failed (thread-safe mode) */
} QueueErr_t;

// ==========================================================================
// Public API Interface
// ==========================================================================

/**
 * @brief   Initialize circular queue
 * @param   q       Pointer to queue handle
 * @param   buffer  External pointer array buffer
 * @param   size    Total buffer size (element count)
 * @return  None
 *
 * @note    IMPORTANT: Usable capacity = size - 1
 *          Classic design to distinguish empty/full states
 * @pre     Buffer must be valid and sufficiently sized
 * @thread_safety No
 */
void Queue_Init(Queue_t *q, void **buffer, uint32_t size);

/**
 * @brief   Enqueue one pointer element
 * @param   q       Queue handle pointer
 * @param   item    Pointer to store (any object type)
 * @return  QueueErr_t Error code
 *
 * @thread_safety Yes (if enabled)
 */
QueueErr_t Queue_Put(Queue_t *q, void *item);

/**
 * @brief   Dequeue one pointer element
 * @param   q       Queue handle pointer
 * @param   item    Output pointer for dequeued element
 * @return  QueueErr_t Error code
 *
 * @thread_safety Yes (if enabled)
 */
QueueErr_t Queue_Get(Queue_t *q, void **item);

/**
 * @brief   Peek head element without dequeuing
 * @param   q       Queue handle pointer
 * @param   item    Output pointer for head element
 * @return  QueueErr_t Error code
 *
 * @thread_safety Yes (if enabled)
 */
QueueErr_t Queue_Peek(Queue_t *q, void **item);

/**
 * @brief   Check if queue is empty
 * @param   q   Queue handle pointer
 * @return  true = empty
 *
 * @thread_safety Yes (if enabled)
 */
bool Queue_IsEmpty(Queue_t *q);

/**
 * @brief   Check if queue is full
 * @param   q   Queue handle pointer
 * @return  true = full
 *
 * @thread_safety Yes (if enabled)
 */
bool Queue_IsFull(Queue_t *q);

/**
 * @brief   Get current element count in queue
 * @param   q   Queue handle pointer
 * @return  Current element count
 *
 * @thread_safety Yes (if enabled)
 */
uint32_t Queue_GetCount(Queue_t *q);

/**
 * @brief   Clear queue (reset pointers, no memory free)
 * @param   q   Queue handle pointer
 *
 * @post    Queue reset to empty state
 * @thread_safety Yes (if enabled)
 */
void Queue_Clear(Queue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* __QUEUE_H */