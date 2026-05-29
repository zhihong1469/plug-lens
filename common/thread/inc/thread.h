/**
 * @file    thread.h
 * @brief   Universal Thread Component (Cross-platform)
 * @details Encapsulate POSIX pthread, support Linux real-time thread (SCHED_FIFO),
 *          thread attribute management, CPU affinity, and thread safety control.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 *
 * @example
 * @code
 * // 1. Initialize thread attributes
 * thread_attr_t attr;
 * thread_attr_init(&attr);
 * attr.name = "RTSP_Push";
 * attr.stack_size = 1024 * 1024;
 *
 * // 2. Create thread
 * thread_t rtsp_thread;
 * thread_create(&rtsp_thread, &attr, rtsp_entry, NULL);
 *
 * // 3. Set Linux real-time priority (optional)
 * thread_set_rt_priority(&rtsp_thread, THREAD_SCHED_FIFO, 90);
 * thread_set_affinity(&rtsp_thread, 0);
 *
 * // 4. Wait for thread exit
 * thread_join(&rtsp_thread, NULL);
 * @endcode
 *
 * @license MIT License
 * Copyright (c) 2026 LuoZhihong
 */

#ifndef __THREAD_H
#define __THREAD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

// ==========================================================================
// Configuration Macro
// ==========================================================================
/**
 * @def THREAD_ENABLE_LOG
 * @brief Enable thread module log output
 */
#define THREAD_ENABLE_LOG 1

#if THREAD_ENABLE_LOG
#include "log.h"
#else
#define LOG_I(...) do{}while(0)
#define LOG_E(...) do{}while(0)
#define LOG_W(...) do{}while(0)
#endif

// ==========================================================================
// Thread Scheduling Policy (Linux Real-time Only)
// ==========================================================================
/**
 * @brief   Thread scheduling policy enumeration
 * @details Used for Linux real-time thread configuration, FIFO is recommended for audio/video services
 */
typedef enum {
    THREAD_SCHED_OTHER  = 0,   /**< Default time-sharing scheduling */
    THREAD_SCHED_FIFO   = 1,   /**< Linux real-time FIFO scheduling (Recommended) */
    THREAD_SCHED_RR     = 2    /**< Linux real-time round-robin scheduling */
} thread_sched_policy_t;

// ==========================================================================
// Thread Priority Enumeration (General Purpose)
// ==========================================================================
/**
 * @brief   General thread priority level
 * @details Automatically mapped to system scheduling priority
 */
typedef enum {
    THREAD_PRIORITY_LOWEST = 0,    /**< Lowest priority */
    THREAD_PRIORITY_LOW,           /**< Low priority */
    THREAD_PRIORITY_NORMAL,        /**< Normal priority (default) */
    THREAD_PRIORITY_HIGH,          /**< High priority */
    THREAD_PRIORITY_HIGHEST,       /**< Highest priority */
    THREAD_PRIORITY_MAX            /**< Enumeration boundary */
} thread_priority_t;

// ==========================================================================
// Thread Attribute Structure
// ==========================================================================
/**
 * @brief   Thread initialization attribute structure
 * @details Configure thread name, stack size, priority, detach state
 */
typedef struct {
    const char          *name;         /**< Thread name (for debugging) */
    size_t              stack_size;    /**< Thread stack size (bytes, 0=system default) */
    thread_priority_t   priority;      /**< Thread general priority */
    bool                detached;       /**< Thread detach state */
    bool                joinable;       /**< Thread joinable state (mutually exclusive with detached) */
} thread_attr_t;

// ==========================================================================
// Thread Handle Structure
// ==========================================================================
/**
 * @brief   Thread control handle (fully encapsulated)
 * @details Manage thread ID, status, attributes and business callback
 */
typedef struct {
    pthread_t           thread_id;     /**< System thread ID */
    thread_attr_t       attr;          /**< Thread runtime attributes */
    void                *(*entry)(void *); /**< Thread entry function */
    void                *user_data;    /**< User-defined private data */
    bool                running;       /**< Thread running flag */
    bool                initialized;   /**< Thread initialization flag */
} thread_t;

// ==========================================================================
// Thread Error Code Enumeration
// ==========================================================================
/**
 * @brief   Thread module error code
 */
typedef enum {
    THREAD_OK = 0,                     /**< Operation successful */
    THREAD_ERR_NULL_PARAM,             /**< Null input parameter */
    THREAD_ERR_INVALID_ATTR,           /**< Invalid thread attribute */
    THREAD_ERR_CREATE_FAILED,          /**< Thread creation failed */
    THREAD_ERR_JOIN_FAILED,            /**< Thread join failed */
    THREAD_ERR_DETACH_FAILED,          /**< Thread detach failed */
    THREAD_ERR_ALREADY_RUNNING,        /**< Thread is already running */
    THREAD_ERR_NOT_RUNNING,            /**< Thread is not running */
    THREAD_ERR_NOT_INITIALIZED         /**< Thread uninitialized */
} thread_err_t;

// ==========================================================================
// Public API
// ==========================================================================

/**
 * @brief   Initialize thread attributes to default values
 * @param   attr    Pointer to thread attribute structure
 * @return  None
 * @note    Must be called before setting custom attributes
 */
void thread_attr_init(thread_attr_t *attr);

/**
 * @brief   Create and start a thread
 * @param   thread      Pointer to thread handle
 * @param   attr        Thread configuration attributes (NULL=use default)
 * @param   entry       Thread business entry function
 * @param   user_data   User data passed to entry function
 * @return  thread_err_t Error code
 * @note    Root privileges required for real-time thread setting
 */
thread_err_t thread_create(thread_t *thread,
                           const thread_attr_t *attr,
                           void *(*entry)(void *),
                           void *user_data);

// ==========================================================================
// One-click Linux Real-time Thread Creation (For AV/AI Services)
// ==========================================================================
/**
 * @brief   Create real-time FIFO thread with one click (auto priority + CPU affinity + naming)
 * @param   thread      Thread handle
 * @param   name        Thread name
 * @param   stack_size  Stack size
 * @param   entry       Entry function
 * @param   user_data   User parameter
 * @param   rt_prio     Real-time priority (1-99)
 * @param   cpu_id      Bind to CPU core
 * @return  Error code
 */
thread_err_t thread_create_rt(thread_t *thread,
                              const char *name,
                              size_t stack_size,
                              void *(*entry)(void *),
                              void *user_data,
                              int rt_prio,
                              uint32_t cpu_id);

/**
 * @brief   Block waiting for thread to exit and recycle resources
 * @param   thread      Pointer to thread handle
 * @param   retval      Output thread return value (NULL=ignore)
 * @return  thread_err_t Error code
 * @note    Only valid for joinable threads
 */
thread_err_t thread_join(thread_t *thread, void **retval);

/**
 * @brief   Detach thread (auto recycle resources after exit)
 * @param   thread      Pointer to thread handle
 * @return  thread_err_t Error code
 */
thread_err_t thread_detach(thread_t *thread);

/**
 * @brief   Check if thread is running
 * @param   thread      Pointer to thread handle
 * @return  true = running, false = not running/exception
 */
bool thread_is_running(thread_t *thread);

/**
 * @brief   Get thread name
 * @param   thread      Pointer to thread handle
 * @return  Thread name string, NULL = uninitialized
 */
const char* thread_get_name(thread_t *thread);

/**
 * @brief   Get current system thread ID
 * @return  pthread_t System thread ID
 */
pthread_t thread_self_id(void);

/**
 * @brief   Thread sleep in milliseconds
 * @param   ms  Sleep milliseconds
 * @return  None
 */
void thread_sleep_ms(uint32_t ms);

/**
 * @brief   Thread sleep in microseconds
 * @param   us  Sleep microseconds
 * @return  None
 */
void thread_sleep_us(uint64_t us);

/**
 * @brief   Actively yield CPU time slice
 * @return  None
 */
void thread_yield(void);

// ==========================================================================
// Linux Extended API (Real-time Thread)
// ==========================================================================
#ifdef __linux__
/**
 * @brief   Set Linux real-time priority (1~99)
 * @param   thread      Pointer to thread handle
 * @param   policy      Scheduling policy (THREAD_SCHED_FIFO recommended)
 * @param   prio        Real-time priority (1~99)
 * @return  thread_err_t Error code
 * @warning Must run with root privileges
 */
thread_err_t thread_set_rt_priority(thread_t *thread, thread_sched_policy_t policy, int prio);

/**
 * @brief   Bind thread to specified CPU core
 * @param   thread      Pointer to thread handle
 * @param   cpu_id      CPU core number
 * @return  thread_err_t Error code
 */
thread_err_t thread_set_affinity(thread_t *thread, uint32_t cpu_id);

/**
 * @brief   Set thread name (visible in top -H)
 * @param   thread      Pointer to thread handle
 * @param   name        Thread name string
 * @return  None
 */
void thread_set_name(thread_t *thread, const char *name);
#endif

/**
 * @brief   Safely stop thread (set running flag)
 * @param   thread      Pointer to thread handle
 * @return  None
 * @note    Business loop needs to check thread_is_running() flag
 */
void thread_stop(thread_t *thread);

#endif /* __THREAD_H */