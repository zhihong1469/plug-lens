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
// ==========================================================================
// 发布模式：开启后，所有日志全关闭，零开销（发布到设备上时打开）
#define RELEASE_MODE    0

// 调试日志开关：开启后DEBUG级日志生效，关闭后只保留ERROR/WARN/INFO
#define DEBUG_ENABLE    1

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
// 日志格式化前缀（自动带文件名、行号、函数名，方便定位）
// ==========================================================================
#define LOG_FMT_ERROR   "[E][%s:%d][%s] "
#define LOG_FMT_WARN    "[W][%s:%d][%s] "
#define LOG_FMT_INFO    "[I][%s:%d][%s] "
#define LOG_FMT_DEBUG   "[D][%s:%d][%s] "

#define LOG_ARGS        __FILE__, __LINE__, __func__

// ==========================================================================
// 运行时日志级别（可选，用于动态调整）
// ==========================================================================
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

// ==========================================================================
// 初始化与反初始化（兼容之前的代码调用，实际上你的宏设计可以不需要，但为了架构完整保留）
// ==========================================================================
int log_init(log_level_t level);
void log_deinit(void);

// ==========================================================================
// 【核心日志宏实现】
// 用 do{...}while(0) 包裹，避免if/else悬挂语法坑，兼容所有C编译器
// 关闭模式下，宏直接展开为空语句，预编译期完全剔除，无任何运行开销
// ==========================================================================

// 错误日志：发布模式也默认保留，用于故障排查
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

// 调试日志
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