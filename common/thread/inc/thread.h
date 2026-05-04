#ifndef __THREAD_H
#define __THREAD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

// ==========================================================================
// 配置宏：是否启用详细日志
// ==========================================================================
#define THREAD_ENABLE_LOG 1

#if THREAD_ENABLE_LOG
#include "log.h"
#endif

// ==========================================================================
// 线程优先级枚举（简化版，映射到系统优先级）
// ==========================================================================
typedef enum {
    THREAD_PRIORITY_LOWEST = 0,
    THREAD_PRIORITY_LOW,
    THREAD_PRIORITY_NORMAL,
    THREAD_PRIORITY_HIGH,
    THREAD_PRIORITY_HIGHEST,
    THREAD_PRIORITY_MAX
} thread_priority_t;

// ==========================================================================
// 线程属性结构体
// ==========================================================================
typedef struct {
    const char *name;               // 线程名称（可选，用于调试）
    size_t stack_size;              // 栈大小（字节，0=使用默认）
    thread_priority_t priority;     // 线程优先级
    bool detached;                  // 是否分离状态（true=自动回收资源）
    bool joinable;                  // 是否可等待（与 detached 互斥）
} thread_attr_t;

// ==========================================================================
// 线程句柄结构体（完全封装）
// ==========================================================================
typedef struct {
    pthread_t thread_id;            // 系统线程ID
    thread_attr_t attr;             // 线程属性
    void *(*entry)(void *);         // 线程入口函数
    void *user_data;                // 用户数据
    bool running;                   // 运行状态标记
    bool initialized;               // 初始化标记
} thread_t;

// ==========================================================================
// 错误码定义
// ==========================================================================
typedef enum {
    THREAD_OK = 0,
    THREAD_ERR_NULL_PARAM,         // 参数为空
    THREAD_ERR_INVALID_ATTR,       // 属性无效
    THREAD_ERR_CREATE_FAILED,      // 创建线程失败
    THREAD_ERR_JOIN_FAILED,        // 等待线程失败
    THREAD_ERR_DETACH_FAILED,      // 分离线程失败
    THREAD_ERR_ALREADY_RUNNING,    // 线程已在运行
    THREAD_ERR_NOT_RUNNING,        // 线程未运行
    THREAD_ERR_NOT_INITIALIZED     // 未初始化
} thread_err_t;

// ==========================================================================
// 对外 API 接口
// ==========================================================================

/**
 * @brief 初始化线程属性为默认值
 * @param attr 线程属性结构体指针
 */
void thread_attr_init(thread_attr_t *attr);

/**
 * @brief 创建并启动线程
 * @param thread 线程句柄指针
 * @param attr 线程属性（NULL=使用默认）
 * @param entry 线程入口函数
 * @param user_data 用户数据（透传给入口函数）
 * @return 错误码
 */
thread_err_t thread_create(thread_t *thread,
                           const thread_attr_t *attr,
                           void *(*entry)(void *),
                           void *user_data);

/**
 * @brief 等待线程结束（阻塞）
 * @param thread 线程句柄指针
 * @param retval 输出参数，线程返回值（NULL=忽略）
 * @return 错误码
 */
thread_err_t thread_join(thread_t *thread, void **retval);

/**
 * @brief 分离线程（线程结束后自动回收资源）
 * @param thread 线程句柄指针
 * @return 错误码
 */
thread_err_t thread_detach(thread_t *thread);

/**
 * @brief 检查线程是否正在运行
 * @param thread 线程句柄指针
 * @return true=正在运行
 */
bool thread_is_running(thread_t *thread);

/**
 * @brief 获取线程名称
 * @param thread 线程句柄指针
 * @return 线程名称（NULL=未设置）
 */
const char* thread_get_name(thread_t *thread);

/**
 * @brief 获取当前系统线程ID
 * @return 线程ID
 */
pthread_t thread_self_id(void);

/**
 * @brief 线程休眠（毫秒）
 * @param ms 休眠毫秒数
 */
void thread_sleep_ms(uint32_t ms);

/**
 * @brief 线程休眠（微秒）
 * @param us 休眠微秒数
 */
void thread_sleep_us(uint64_t us);

/**
 * @brief 让出CPU时间片
 */
void thread_yield(void);

#endif /* __THREAD_H */
