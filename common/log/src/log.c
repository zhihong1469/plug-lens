// common/log/src/log.c
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ==========================================================================
// 内部静态变量
// ==========================================================================
static log_level_t g_current_level = LOG_LEVEL_INFO;
static int g_initialized = 0;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

// ==========================================================================
// 对外API实现
// ==========================================================================

int log_init(log_level_t level)
{
    pthread_mutex_lock(&g_log_lock);
    
    if (g_initialized) {
        pthread_mutex_unlock(&g_log_lock);
        return 0; // 重复初始化，直接返回
    }

    g_current_level = level;
    g_initialized = 1;
    
    pthread_mutex_unlock(&g_log_lock);
    
    // 打印初始化信息（用标准输出，避免宏递归）
    fprintf(stdout, "[I][log.c:0][log_init] Log system initialized (level=%d)\n", level);
    return 0;
}

void log_deinit(void)
{
    pthread_mutex_lock(&g_log_lock);
    
    if (!g_initialized) {
        pthread_mutex_unlock(&g_log_lock);
        return;
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
    // 这里不加锁，因为是原子读取，性能优先
    // 如果需要绝对线程安全，可以加锁
    return g_current_level;
}