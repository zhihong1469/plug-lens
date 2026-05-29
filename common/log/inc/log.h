/* SPDX-License-Identifier: MIT */
/**
 * @file    log.h
 * @brief   Embedded Linux log system for plug-lens Vision AI terminal
 * @details Core capabilities:
 *          1. Compile-time & runtime log level control
 *          2. Dual output: console + file (SD card storage)
 *          3. Daemon mode support for background service
 *          4. Thread-safe printing with mutex protection
 *          5. Auto log rotation and directory creation
 *          6. Release/Debug mode one-click switching
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. Call log_init() before using any log macros
 *          2. SD card must be mounted for file logging
 *          3. Log macros are disabled at compile time in release mode
 *          4. All public APIs are thread-safe
 */
#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>
#include <stdint.h>
#include "config_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// Log Level Enumeration
// ==========================================================================
/**
 * @brief   Log output level definition
 * @details Level hierarchy: None < Error < Warn < Info < Debug < All
 */
typedef enum {
    LOG_LEVEL_NONE = 0,    /**< No log output */
    LOG_LEVEL_ERROR,       /**< Error level (critical failures) */
    LOG_LEVEL_WARN,        /**< Warning level (non-critical issues) */
    LOG_LEVEL_INFO,        /**< Information level (normal status) */
    LOG_LEVEL_DEBUG,       /**< Debug level (development details) */
    LOG_LEVEL_ALL          /**< All log levels enabled */
} log_level_t;

// ==========================================================================
// Core Switch: One-click Debug/Release Mode Toggle
// ==========================================================================
/** Release mode flag (1 = release, 0 = debug) */
#define RELEASE_MODE    0
/** Debug log enable flag */
#define DEBUG_ENABLE    0

/** Compile-time log level configuration */
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
// Log Format Prefix
// ==========================================================================
/** Error log format prefix (file:line:function) */
#define LOG_FMT_ERROR   "[E][%s:%d][%s] "
/** Warning log format prefix */
#define LOG_FMT_WARN    "[W][%s:%d][%s] "
/** Info log format prefix */
#define LOG_FMT_INFO    "[I][%s:%d][%s] "
/** Debug log format prefix */
#define LOG_FMT_DEBUG   "[D][%s:%d][%s] "

/** Standard log arguments: file name, line number, function name */
#define LOG_ARGS        __FILE__, __LINE__, __func__

// ==========================================================================
// Public APIs
// ==========================================================================
/**
 * @brief   Initialize log system
 * @param   level   Runtime log output level
 * @return  0 on success, -1 on failure (SD card not mounted/file error)
 *
 * @pre     SD card must be mounted at /mnt/sdcard
 * @post    Log system ready, file descriptor opened
 * @note    Call once at system initialization
 * @thread_safety Yes
 */
int log_init(log_level_t level);

/**
 * @brief   Deinitialize log system and release resources
 *
 * @pre     Log system has been initialized
 * @post    File closed, mutex destroyed
 * @thread_safety Yes
 */
void log_deinit(void);

/**
 * @brief   Set runtime log output level
 * @param   level   New log level to apply
 *
 * @pre     Log system initialized
 * @thread_safety Yes
 */
void log_set_level(log_level_t level);

/**
 * @brief   Get current runtime log level
 * @return  Current active log level
 *
 * @thread_safety Yes
 */
log_level_t log_get_level(void);

/**
 * @brief   Set daemon mode for log output
 * @param   is_daemon  1 = daemon mode (file only), 0 = console + file
 *
 * @details Disable console output in daemon mode for background service
 * @pre     Log system initialized
 * @thread_safety Yes
 */
void log_set_daemon_mode(int is_daemon);

// ==========================================================================
// Core Log Macros (Compile-time Optimized)
// ==========================================================================
/**
 * @brief   Error log macro
 * @param   fmt   Format string
 * @param   ...   Variable arguments
 */
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_E(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_ERROR) { \
        log_printf(LOG_FMT_ERROR fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_E(fmt, ...) do { } while(0)
#endif

/**
 * @brief   Warning log macro
 * @param   fmt   Format string
 * @param   ...   Variable arguments
 */
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_W(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_WARN) { \
        log_printf(LOG_FMT_WARN fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_W(fmt, ...) do { } while(0)
#endif

/**
 * @brief   Info log macro
 * @param   fmt   Format string
 * @param   ...   Variable arguments
 */
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_I(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_INFO) { \
        log_printf(LOG_FMT_INFO fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_I(fmt, ...) do { } while(0)
#endif

/**
 * @brief   Debug log macro
 * @param   fmt   Format string
 * @param   ...   Variable arguments
 */
#if COMPILE_LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_D(fmt, ...) do { \
    if (log_get_level() >= LOG_LEVEL_DEBUG) { \
        log_printf(LOG_FMT_DEBUG fmt "\n", LOG_ARGS, ##__VA_ARGS__); \
    } \
} while(0)
#else
#define LOG_D(fmt, ...) do { } while(0)
#endif

/**
 * @brief   Internal log print function (private)
 * @param   fmt   Format string
 * @param   ...   Variable arguments
 * @note    Do NOT call this function directly, use LOG_* macros
 */
void log_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __LOG_H */