/**
 * @file    thread.h
 * @brief   Universal Thread Component (Cross-platform)
 *          通用线程组件（跨平台）
 * @details Encapsulate POSIX pthread, support Linux real-time thread (SCHED_FIFO),
 *          thread attribute management, CPU affinity, and thread safety control.
 *          封装POSIX pthread，支持Linux实时线程(SCHED_FIFO)、线程属性管理、CPU亲和性、线程安全控制
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 *
 * @example
 * @code
 * // 1. 初始化线程属性
 * thread_attr_t attr;
 * thread_attr_init(&attr);
 * attr.name = "RTSP_Push";
 * attr.stack_size = 1024 * 1024;
 *
 * // 2. 创建线程
 * thread_t rtsp_thread;
 * thread_create(&rtsp_thread, &attr, rtsp_entry, NULL);
 *
 * // 3. 设置Linux实时优先级（可选）
 * thread_set_rt_priority(&rtsp_thread, THREAD_SCHED_FIFO, 90);
 * thread_set_affinity(&rtsp_thread, 0);
 *
 * // 4. 等待线程退出
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
// Configuration Macro | 配置宏
// ==========================================================================
/**
 * @def THREAD_ENABLE_LOG
 * @brief Enable thread module log output
 *        启用线程模块日志输出
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
// 线程调度策略（仅Linux实时线程使用）
// ==========================================================================
/**
 * @brief   Thread scheduling policy enumeration
 *          线程调度策略枚举
 * @details Used for Linux real-time thread configuration, FIFO is recommended for audio/video services
 *          用于Linux实时线程配置，音视频服务推荐使用FIFO
 */
typedef enum {
    THREAD_SCHED_OTHER  = 0,   /**< Default time-sharing scheduling | 默认分时调度（普通线程） */
    THREAD_SCHED_FIFO   = 1,   /**< Linux real-time FIFO scheduling | Linux实时FIFO调度（推荐） */
    THREAD_SCHED_RR     = 2    /**< Linux real-time round-robin scheduling | Linux实时轮询调度 */
} thread_sched_policy_t;

// ==========================================================================
// Thread Priority Enumeration (General Purpose)
// 线程优先级枚举（通用）
// ==========================================================================
/**
 * @brief   General thread priority level
 *          通用线程优先级等级
 * @details Map to system scheduling priority automatically
 *          自动映射到系统调度优先级
 */
typedef enum {
    THREAD_PRIORITY_LOWEST = 0,    /**< Lowest priority | 最低优先级 */
    THREAD_PRIORITY_LOW,           /**< Low priority | 低优先级 */
    THREAD_PRIORITY_NORMAL,        /**< Normal priority (default) | 普通优先级（默认） */
    THREAD_PRIORITY_HIGH,          /**< High priority | 高优先级 */
    THREAD_PRIORITY_HIGHEST,       /**< Highest priority | 最高优先级 */
    THREAD_PRIORITY_MAX            /**< Enumeration boundary | 枚举边界 */
} thread_priority_t;

// ==========================================================================
// Thread Attribute Structure
// 线程属性结构体
// ==========================================================================
/**
 * @brief   Thread initialization attribute structure
 *          线程初始化属性结构体
 * @details Configure thread name, stack size, priority, detach state
 *          配置线程名称、栈大小、优先级、分离状态
 */
typedef struct {
    const char          *name;         /**< Thread name (for debugging) | 线程名称（调试用） */
    size_t              stack_size;    /**< Thread stack size (bytes, 0=system default) | 线程栈大小(字节，0=系统默认) */
    thread_priority_t   priority;      /**< Thread general priority | 线程通用优先级 */
    bool                detached;       /**< Thread detach state | 线程分离状态 */
    bool                joinable;       /**< Thread joinable state (mutually exclusive with detached) | 线程可汇合状态（与分离互斥） */
} thread_attr_t;

// ==========================================================================
// Thread Handle Structure
// 线程句柄结构体
// ==========================================================================
/**
 * @brief   Thread control handle (fully encapsulated)
 *          线程控制句柄（完全封装）
 * @details Manage thread ID, status, attributes and business callback
 *          管理线程ID、状态、属性、业务回调
 */
typedef struct {
    pthread_t           thread_id;     /**< System thread ID | 系统线程ID */
    thread_attr_t       attr;          /**< Thread runtime attributes | 线程运行属性 */
    void                *(*entry)(void *); /**< Thread entry function | 线程入口函数 */
    void                *user_data;    /**< User-defined private data | 用户自定义私有数据 */
    bool                running;       /**< Thread running flag | 线程运行标志 */
    bool                initialized;   /**< Thread initialization flag | 线程初始化标志 */
} thread_t;

// ==========================================================================
// Thread Error Code Enumeration
// 线程错误码枚举
// ==========================================================================
/**
 * @brief   Thread module error code
 *          线程模块错误码
 */
typedef enum {
    THREAD_OK = 0,                     /**< Operation successful | 操作成功 */
    THREAD_ERR_NULL_PARAM,             /**< Null input parameter | 输入参数为空 */
    THREAD_ERR_INVALID_ATTR,           /**< Invalid thread attribute | 无效的线程属性 */
    THREAD_ERR_CREATE_FAILED,          /**< Thread creation failed | 线程创建失败 */
    THREAD_ERR_JOIN_FAILED,            /**< Thread join failed | 线程等待失败 */
    THREAD_ERR_DETACH_FAILED,          /**< Thread detach failed | 线程分离失败 */
    THREAD_ERR_ALREADY_RUNNING,        /**< Thread is already running | 线程已在运行 */
    THREAD_ERR_NOT_RUNNING,            /**< Thread is not running | 线程未运行 */
    THREAD_ERR_NOT_INITIALIZED         /**< Thread uninitialized | 线程未初始化 */
} thread_err_t;

// ==========================================================================
// Public API | 对外接口
// ==========================================================================

/**
 * @brief   Initialize thread attributes to default values
 *          初始化线程属性为默认值
 * @param   attr    Pointer to thread attribute structure | 线程属性结构体指针
 * @return  None
 * @note    Must be called before setting custom attributes
 *          设置自定义属性前必须调用
 */
void thread_attr_init(thread_attr_t *attr);

/**
 * @brief   Create and start a thread
 *          创建并启动线程
 * @param   thread  Pointer to thread handle | 线程句柄指针
 * @param   attr    Thread configuration attributes (NULL=use default) | 线程配置属性（NULL=使用默认）
 * @param   entry   Thread business entry function | 线程业务入口函数
 * @param   user_data User data passed to entry function | 传递给入口函数的用户数据
 * @return  thread_err_t Error code | 错误码
 * @note    Root privileges required for real-time thread setting
 *          设置实时线程需要root权限
 */
thread_err_t thread_create(thread_t *thread,
                           const thread_attr_t *attr,
                           void *(*entry)(void *),
                           void *user_data);

// ==========================================================================
// 【业务优化】一键创建 Linux 实时线程（音视频/AI专用）
// ==========================================================================
/**
 * @brief   一键创建实时FIFO线程（自动设置：优先级+CPU亲和+命名）
 * @param   thread      线程句柄
 * @param   name        线程名称
 * @param   stack_size  栈大小
 * @param   entry       入口函数
 * @param   user_data   用户参数
 * @param   rt_prio     实时优先级(1-99)
 * @param   cpu_id      绑定CPU核心
 * @return  错误码
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
 *          阻塞等待线程退出并回收资源
 * @param   thread  Pointer to thread handle | 线程句柄指针
 * @param   retval  Output thread return value (NULL=ignore) | 输出线程返回值（NULL=忽略）
 * @return  thread_err_t Error code | 错误码
 * @note    Only valid for joinable threads
 *          仅对可汇合线程有效
 */
thread_err_t thread_join(thread_t *thread, void **retval);

/**
 * @brief   Detach thread (auto recycle resources after exit)
 *          分离线程（退出后自动回收资源）
 * @param   thread  Pointer to thread handle | 线程句柄指针
 * @return  thread_err_t Error code | 错误码
 */
thread_err_t thread_detach(thread_t *thread);

/**
 * @brief   Check if thread is running
 *          检查线程是否正在运行
 * @param   thread  Pointer to thread handle | 线程句柄指针
 * @return  true = running, false = not running/exception
 *          true=运行中，false=未运行/异常
 */
bool thread_is_running(thread_t *thread);

/**
 * @brief   Get thread name
 *          获取线程名称
 * @param   thread  Pointer to thread handle | 线程句柄指针
 * @return  Thread name string, NULL = uninitialized
 *          线程名称字符串，NULL=未初始化
 */
const char* thread_get_name(thread_t *thread);

/**
 * @brief   Get current system thread ID
 *          获取当前系统线程ID
 * @return  pthread_t System thread ID | 系统线程ID
 */
pthread_t thread_self_id(void);

/**
 * @brief   Thread sleep in milliseconds
 *          线程毫秒级休眠
 * @param   ms  Sleep milliseconds | 休眠毫秒数
 * @return  None
 */
void thread_sleep_ms(uint32_t ms);

/**
 * @brief   Thread sleep in microseconds
 *          线程微秒级休眠
 * @param   us  Sleep microseconds | 休眠微秒数
 * @return  None
 */
void thread_sleep_us(uint64_t us);

/**
 * @brief   Actively yield CPU time slice
 *          主动让出CPU时间片
 * @return  None
 */
void thread_yield(void);

// ==========================================================================
// Linux Extended API (Real-time Thread)
// Linux扩展接口（实时线程专用）
// ==========================================================================
#ifdef __linux__
/**
 * @brief   Set Linux real-time priority (1~99)
 *          设置Linux精确实时优先级(1~99)
 * @param   thread  Pointer to thread handle | 线程句柄指针
 * @param   policy  Scheduling policy (THREAD_SCHED_FIFO recommended) | 调度策略（推荐THREAD_SCHED_FIFO）
 * @param   prio    Real-time priority (1~99) | 实时优先级(1~99)
 * @return  thread_err_t Error code | 错误码
 * @warning Must run with root privileges
 *          必须使用root权限运行
 */
thread_err_t thread_set_rt_priority(thread_t *thread, thread_sched_policy_t policy, int prio);

/**
 * @brief   Bind thread to specified CPU core
 *          绑定线程到指定CPU核心
 * @param   thread  Pointer to thread handle | 线程句柄指针
 * @param   cpu_id  CPU core number (i.MX6ULL fill 0) | CPU核心编号（i.MX6ULL填0）
 * @return  thread_err_t Error code | 错误码
 * @note     cat /proc/cpuinfo | grep processor
 */
thread_err_t thread_set_affinity(thread_t *thread, uint32_t cpu_id);

/**
 * @brief   Set thread name (visible in top -H)
 *          设置线程名称（top -H可查看）
 * @param   thread  Pointer to thread handle | 线程句柄指针
 * @param   name    Thread name string | 线程名称字符串
 * @return  None
 */
void thread_set_name(thread_t *thread, const char *name);
#endif

/**
 * @brief   Safely stop thread (set running flag)
 *          安全停止线程（设置运行标志）
 * @param   thread  Pointer to thread handle | 线程句柄指针
 * @return  None
 * @note    Business loop needs to check thread_is_running() flag
 *          业务循环需要判断thread_is_running()标志
 */
void thread_stop(thread_t *thread);

#endif /* __THREAD_H */