#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>
#include <stdint.h>

// ==========================================================================
// 日志配置（工业级可配置）
// ==========================================================================
#define LOG_FILE_PATH     "/var/log/app.log"   // 工业级日志文件路径
#define LOG_MAX_FILE_SIZE (10 * 1024 * 1024)    // 10MB日志滚动阈值

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
// ==========================================================================
#define RELEASE_MODE    0
#define DEBUG_ENABLE    0

// 编译时日志级别控制
#if RELEASE_MODE
    #define COMPILE_LOG_LEVEL LOG_LEVEL_NONE
#else
    #if DEBUG_ENABLE
        #define COMPILE_LOG_LEVEL LOG_LEVEL_DEBUG
    #else
        #define COMPILE_LOG_LEVEL LOG_LEVEL_INFO
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
// 对外接口（兼容原有 + 新增守护进程模式）
// ==========================================================================
// 原有接口（兼容不变）
int log_init(log_level_t level);
void log_deinit(void);
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

// 【新增】守护进程模式初始化（工业级核心）
void log_set_daemon_mode(int is_daemon);

// ==========================================================================
// 【核心】日志宏（完全兼容原有业务代码！）
// ==========================================================================
// 错误日志
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_E(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_ERROR) { \
        log_printf(LOG_FMT_ERROR fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_E(fmt, ...) do { } while(0)
#endif

// 警告日志
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_W(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_WARN) { \
        log_printf(LOG_FMT_WARN fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_W(fmt, ...) do { } while(0)
#endif

// 信息日志
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_I(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_INFO) { \
        log_printf(LOG_FMT_INFO fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_I(fmt, ...) do { } while(0)
#endif

// 调试日志
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_D(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_DEBUG) { \
        log_printf(LOG_FMT_DEBUG fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_D(fmt, ...) do { } while(0)
#endif

// 内部打印函数（对外隐藏）
void log_printf(const char *fmt, ...);

#endif /* __LOG_H */