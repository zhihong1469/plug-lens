#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>

// ==========================================================================
// 【核心开关：一键切换调试/发布模式】
// ==========================================================================
// 发布模式：开启后，所有日志全关闭，零开销（发布到设备上时打开）
#define RELEASE_MODE    0

// 调试日志开关：开启后DEBUG级日志生效，关闭后只保留ERROR/WARN/INFO
#define DEBUG_ENABLE    1

// ==========================================================================
// 日志格式化前缀（自动带文件名、行号、函数名，方便定位）
// ==========================================================================
#define LOG_FMT_ERROR   "[E][%s:%d][%s] "
#define LOG_FMT_WARN    "[W][%s:%d][%s] "
#define LOG_FMT_INFO    "[I][%s:%d][%s] "
#define LOG_FMT_DEBUG   "[D][%s:%d][%s] "

#define LOG_ARGS        __FILE__, __LINE__, __func__

// ==========================================================================
// 【核心日志宏实现】
// 用 do{...}while(0) 包裹，避免if/else悬挂语法坑，兼容所有C编译器
// 关闭模式下，宏直接展开为空语句，预编译期完全剔除，无任何运行开销
// ==========================================================================

// 错误日志：发布模式也默认保留，用于故障排查（可自行关闭）
#if RELEASE_MODE
#define LOG_E(fmt, ...) do { } while(0)
#else
#define LOG_E(fmt, ...) printf(LOG_FMT_ERROR fmt "\n", LOG_ARGS, ##__VA_ARGS__)
#endif

// 警告日志
#if RELEASE_MODE
#define LOG_W(fmt, ...) do { } while(0)
#else
#define LOG_W(fmt, ...) printf(LOG_FMT_WARN fmt "\n", LOG_ARGS, ##__VA_ARGS__)
#endif

// 信息日志
#if RELEASE_MODE
#define LOG_I(fmt, ...) do { } while(0)
#else
#define LOG_I(fmt, ...) printf(LOG_FMT_INFO fmt "\n", LOG_ARGS, ##__VA_ARGS__)
#endif

// 调试日志：单独开关，关闭后完全剔除
#if RELEASE_MODE || !DEBUG_ENABLE
#define LOG_D(fmt, ...) do { } while(0)
#else
#define LOG_D(fmt, ...) printf(LOG_FMT_DEBUG fmt "\n", LOG_ARGS, ##__VA_ARGS__)
#endif

#endif /* __LOG_H */