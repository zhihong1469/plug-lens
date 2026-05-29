/* SPDX-License-Identifier: MIT */
/**
 * @file    mem_adapter.h
 * @brief   Cross-platform thread-safe memory management adapter layer
 * @details Core capabilities for plug-lens Vision AI terminal:
 *          1. Dual-mode support: TLSF high-performance static pool / Linux native malloc
 *          2. Compile-time switch (USE_TLSF) with zero runtime performance loss
 *          3. Standard APIs: alloc/calloc/memalign/free (fully compatible with libc)
 *          4. Built-in thread mutex for safe multi-thread concurrent access
 *          5. Optimized for frame buffer, DataBus, AI inference, and video processing
 *          6. Unified interface hides underlying allocator differences
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. Call mem_init() once at system startup before any allocation
 *          2. USE_TLSF=1 for mass production (embedded Linux), 0 for debug
 *          3. All public APIs are thread-safe
 *          4. Only free pointers allocated by this module
 */
#ifndef __MEM_ADAPTER_H__
#define __MEM_ADAPTER_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// @brief   Compile-time memory mode selection
// @note    1 = TLSF static memory pool (production/embedded Linux)
// @note    0 = Linux native malloc/free (development/debug)
// ==========================================================================
#define USE_TLSF    1

// ==========================================================================
// Public API Declarations
// ==========================================================================

/**
 * @brief   Initialize memory adapter layer
 * @param   pool        Static memory pool start address (required for TLSF, NULL for native)
 * @param   pool_size   Total size of static memory pool (valid for TLSF only)
 * @return  None
 *
 * @pre     Called once at system initialization, before any memory operations
 * @post    Memory allocator ready for use
 * @thread_safety No
 */
void mem_init(void *pool, size_t pool_size);

/**
 * @brief   Destroy memory adapter and release underlying resources
 * @return  None
 *
 * @pre     System shutdown stage
 * @post    All allocator resources released
 * @thread_safety No
 */
void mem_destroy(void);

/**
 * @brief   Allocate memory (equivalent to standard malloc, no zero-initialization)
 * @param   size    Number of bytes to allocate
 * @return  Valid pointer on success, NULL on failure
 *
 * @note    Uninitialized (dirty data) for optimal performance
 * @note    Recommended for video frames and data buffers
 * @thread_safety Yes
 */
void *mem_alloc(size_t size);

/**
 * @brief   Allocate zero-initialized memory (equivalent to standard calloc)
 * @param   num     Number of elements
 * @param   size    Size of one element in bytes
 * @return  Valid zero-initialized pointer on success, NULL on failure
 *
 * @note    Includes memset overhead, use only for state structs/counters
 * @thread_safety Yes
 */
void *mem_calloc(size_t num, size_t size);

/**
 * @brief   Allocate aligned memory (hardware/AI/video optimized)
 * @param   align   Alignment bytes (power of two: 32/64 recommended)
 * @param   size    Number of bytes to allocate
 * @return  Aligned pointer on success, NULL on failure
 *
 * @note    Mandatory for camera frames, NPU/AI inference, DMA operations
 * @thread_safety Yes
 */
void *mem_memalign(size_t align, size_t size);

/**
 * @brief   Free allocated memory (equivalent to standard free)
 * @param   ptr     Pointer to memory to free
 * @return  None
 *
 * @note    NULL input is safely ignored
 * @note    Do not mix with standard free() or other allocators
 * @thread_safety Yes
 */
void mem_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* __MEM_ADAPTER_H__ */