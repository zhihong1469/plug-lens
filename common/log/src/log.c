/* SPDX-License-Identifier: MIT */
/**
 * @file    log.c
 * @brief   Thread-safe log system implementation for embedded Linux
 * @details Core implementation features:
 *          1. Mutex protection for multi-thread log printing
 *          2. Recursive directory creation (no shell dependency)
 *          3. SD card mount detection for industrial reliability
 *          4. Daemon mode console output control
 *          5. Automatic log file rotation by size
 *          6. Dual output (file + console) for debug
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

// Global log control block
static log_level_t g_current_level = LOG_LEVEL_INFO;  /**< Current runtime log level */
static int g_initialized = 0;                         /**< Log system init status flag */
static int g_is_daemon = 0;                           /**< Daemon mode flag */
static FILE *g_log_file = NULL;                       /**< Log file descriptor */
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;  /**< Thread safety mutex */

// ===================== Embedded Optimized Directory Creation =====================
/**
 * @brief   Recursive directory creation (pure syscall, no shell dependency)
 * @param   dir   Target directory path
 * @return  0 on success, -1 on failure
 * @note    Embedded-specific fix: No dependency on system mkdir -p command
 */
static int log_mkdir(const char *dir)
{
    char tmp[256];
    char *p = NULL;
    size_t len;

    // Null path and length check
    if (!dir || strlen(dir) >= sizeof(tmp)) {
        printf("[LOG ERROR] Path too long or NULL\n");
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);

    // Remove trailing slash
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    // Recursive directory creation
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (access(tmp, F_OK) != 0) {
                if (mkdir(tmp, 0755) == -1 && errno != EEXIST) {
                    printf("[LOG ERROR] mkdir %s failed: %s\n", tmp, strerror(errno));
                    return -1;
                }
            }
            *p = '/';
        }
    }

    // Create final directory level
    if (access(tmp, F_OK) != 0) {
        if (mkdir(tmp, 0755) == -1 && errno != EEXIST) {
            printf("[LOG ERROR] mkdir %s failed: %s\n", tmp, strerror(errno));
            return -1;
        }
    }

    printf("[LOG] Create dir success: %s\n", tmp);
    return 0;
}

/**
 * @brief   Create parent directory from file path
 * @param   file_path   Full log file path
 * @return  0 on success, -1 on failure
 */
static int log_create_dir_by_file(const char *file_path)
{
    char path[256];
    char *last_slash;

    if (!file_path) return -1;
    strncpy(path, file_path, sizeof(path)-1);

    // Find last slash and truncate to directory path
    last_slash = strrchr(path, '/');
    if (!last_slash) return -1;
    *last_slash = '\0';

    return log_mkdir(path);
}

/**
 * @brief   Log file rotation by size limit
 * @details Rename full log to .old and create new file
 */
static void log_rotate(void)
{
    if (!g_log_file) return;

    struct stat st;
    if (stat(LOG_FILE_PATH, &st) == 0 && st.st_size > LOG_MAX_FILE_SIZE)
    {
        fclose(g_log_file);
        char backup[256];
        snprintf(backup, sizeof(backup), "%s.old", LOG_FILE_PATH);
        rename(LOG_FILE_PATH, backup);
        g_log_file = fopen(LOG_FILE_PATH, "a+");
    }
}

/**
 * @brief   Public API: Log system initialization
 */
int log_init(log_level_t level)
{
    pthread_mutex_lock(&g_log_lock);

    if (g_initialized) {
        pthread_mutex_unlock(&g_log_lock);
        return 0;
    }

    // Critical check: SD card mount verification
    if (access("/mnt/sdcard", F_OK) != 0) {
        printf("[LOG FATAL] SD card not mounted! Execute: mount /dev/mmcblk0p1 /mnt/sdcard\n");
        pthread_mutex_unlock(&g_log_lock);
        return -1;
    }

    // Auto create log directory
    if (log_create_dir_by_file(LOG_FILE_PATH) != 0) {
        printf("[LOG FATAL] Failed to create log directory\n");
        pthread_mutex_unlock(&g_log_lock);
        return -1;
    }

    // Open log file in append mode
    g_log_file = fopen(LOG_FILE_PATH, "a+");
    if (!g_log_file) {
        printf("[LOG FATAL] Failed to open log file: %s, error: %s\n", LOG_FILE_PATH, strerror(errno));
        pthread_mutex_unlock(&g_log_lock);
        return -1;
    }

    g_current_level = level;
    g_initialized = 1;
    g_is_daemon = 0;

    printf("[LOG] Initialization success: %s\n", LOG_FILE_PATH);

    pthread_mutex_unlock(&g_log_lock);
    return 0;
}

/**
 * @brief   Public API: Set daemon output mode
 */
void log_set_daemon_mode(int is_daemon)
{
    pthread_mutex_lock(&g_log_lock);
    g_is_daemon = is_daemon;
    pthread_mutex_unlock(&g_log_lock);
}

/**
 * @brief   Public API: Deinitialize log system
 */
void log_deinit(void)
{
    pthread_mutex_lock(&g_log_lock);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_log_lock);
        return;
    }

    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_initialized = 0;
    pthread_mutex_unlock(&g_log_lock);
}

/**
 * @brief   Public API: Set runtime log level
 */
void log_set_level(log_level_t level)
{
    pthread_mutex_lock(&g_log_lock);
    g_current_level = level;
    pthread_mutex_unlock(&g_log_lock);
}

/**
 * @brief   Public API: Get current log level
 */
log_level_t log_get_level(void)
{
    return g_current_level;
}

/**
 * @brief   Internal: Thread-safe log print implementation
 */
void log_printf(const char *fmt, ...)
{
    if (!g_initialized) return;

    pthread_mutex_lock(&g_log_lock);
    log_rotate();

    va_list ap;
    va_start(ap, fmt);

    // Daemon mode: file output only
    if (g_is_daemon) {
        if (g_log_file) {
            vfprintf(g_log_file, fmt, ap);
            fflush(g_log_file);
        }
    } else {
        // Normal mode: file + console output
        if (g_log_file) {
            vfprintf(g_log_file, fmt, ap);
            fflush(g_log_file);
        }
        vprintf(fmt, ap);
    }

    va_end(ap);
    pthread_mutex_unlock(&g_log_lock);
}