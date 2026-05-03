// common/log/inc/log.h
#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>
#include <stdint.h>

// ==========================================================================
// 日志级别定义
// ==========================================================================
typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_ALL
} log_level_t;

// ==========================================================================
// 【核心开关：一键切换调试/发布模式】
// 【修改】为了解决日志刷屏，默认关闭 DEBUG 日志
// ==========================================================================
#define RELEASE_MODE    0

// 调试日志开关：0=关闭(默认)，1=开启
#define DEBUG_ENABLE    0

// 编译时日志级别控制
#if RELEASE_MODE
    #define COMPILE_LOG_LEVEL LOG_LEVEL_NONE
#else
    #if DEBUG_ENABLE
        #define COMPILE_LOG_LEVEL LOG_LEVEL_DEBUG
    #else
        #define COMPILE_LOG_LEVEL LOG_LEVEL_INFO  // 【修改】默认只打到 INFO
    #endif
#endif

// ==========================================================================
// 日志格式化前缀
// ==========================================================================
#define LOG_FMT_ERROR   "[E][%s:%d][%s] "
#define LOG_FMT_WARN    "[W][%s:%d][%s] "
#define LOG_FMT_INFO    "[I][%s:%d][%s] "
#define LOG_FMT_DEBUG   "[D][%s:%d][%s] "

#define LOG_ARGS        __FILE__, __LINE__, __func__

// ==========================================================================
// 运行时日志级别
// ==========================================================================
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

int log_init(log_level_t level);
void log_deinit(void);

// ==========================================================================
// 【核心】日志宏实现
// ==========================================================================

// 错误日志：永远保留
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_E(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_ERROR) { \
        printf(LOG_FMT_ERROR fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_E(fmt, ...) do { } while(0)
#endif

// 警告日志
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_W(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_WARN) { \
        printf(LOG_FMT_WARN fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_W(fmt, ...) do { } while(0)
#endif

// 信息日志
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_I(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_INFO) { \
        printf(LOG_FMT_INFO fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_I(fmt, ...) do { } while(0)
#endif

// 调试日志：默认关闭
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_D(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_DEBUG) { \
        printf(LOG_FMT_DEBUG fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_D(fmt, ...) do { } while(0)
#endif

#endif /* __LOG_H */