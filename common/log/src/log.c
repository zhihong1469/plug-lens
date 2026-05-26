#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/stat.h>

// 全局日志控制块（线程安全）
static log_level_t g_current_level = LOG_LEVEL_INFO;
static int g_initialized = 0;
static int g_is_daemon = 0;           // 【核心】守护进程模式标志
static FILE *g_log_file = NULL;       // 日志文件句柄
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

// 日志文件滚动（工业级基础功能）
static void log_rotate(void)
{
    if (!g_log_file) return;

    struct stat st;
    if (stat(LOG_FILE_PATH, &st) == 0 && st.st_size > LOG_MAX_FILE_SIZE)
    {
        fclose(g_log_file);
        char backup_path[256];
        snprintf(backup_path, sizeof(backup_path), "%s.old", LOG_FILE_PATH);
        rename(LOG_FILE_PATH, backup_path);
        g_log_file = fopen(LOG_FILE_PATH, "a+");
    }
}

int log_init(log_level_t level)
{
    pthread_mutex_lock(&g_log_lock);

    if (g_initialized) {
        pthread_mutex_unlock(&g_log_lock);
        return 0;
    }

    // 打开日志文件（追加模式）
    g_log_file = fopen(LOG_FILE_PATH, "a+");
    if (!g_log_file) {
        pthread_mutex_unlock(&g_log_lock);
        return -1;
    }

    g_current_level = level;
    g_initialized = 1;
    g_is_daemon = 0;  // 默认非守护进程模式

    pthread_mutex_unlock(&g_log_lock);

    // 初始化日志（仅终端打印）
    log_printf("[I][log.c:0][log_init] Log system initialized (level=%d, daemon=%d)\n",
               level, g_is_daemon);
    return 0;
}

// 【新增】设置守护进程模式
void log_set_daemon_mode(int is_daemon)
{
    pthread_mutex_lock(&g_log_lock);
    g_is_daemon = is_daemon;
    pthread_mutex_unlock(&g_log_lock);
}

void log_deinit(void)
{
    pthread_mutex_lock(&g_log_lock);

    if (!g_initialized) {
        pthread_mutex_unlock(&g_log_lock);
        return;
    }

    // 关闭日志文件
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }

    g_initialized = 0;
    pthread_mutex_unlock(&g_log_lock);
}

void log_set_level(log_level_t level)
{
    pthread_mutex_lock(&g_log_lock);
    g_current_level = level;
    pthread_mutex_unlock(&g_log_lock);
}

log_level_t log_get_level(void)
{
    return g_current_level;
}

// 【核心】统一日志输出函数
void log_printf(const char *fmt, ...)
{
    if (!g_initialized) return;

    pthread_mutex_lock(&g_log_lock);

    // 1. 日志文件滚动
    log_rotate();

    va_list ap;
    va_start(ap, fmt);

    // 2. 守护进程模式：仅输出文件，不输出终端
    if (g_is_daemon)
    {
        if (g_log_file) {
            vfprintf(g_log_file, fmt, ap);
            fflush(g_log_file);  // 立即刷盘，工业级可靠性
        }
    }
    // 3. 普通模式：输出文件 + 终端
    else
    {
        if (g_log_file) {
            vfprintf(g_log_file, fmt, ap);
            fflush(g_log_file);
        }
        vprintf(fmt, ap);  // 终端打印
    }

    va_end(ap);
    pthread_mutex_unlock(&g_log_lock);
}