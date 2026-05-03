// common/log/src/log.c
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static log_level_t g_current_level = LOG_LEVEL_INFO;
static int g_initialized = 0;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

int log_init(log_level_t level)
{
    pthread_mutex_lock(&g_log_lock);
    
    if (g_initialized) {
        pthread_mutex_unlock(&g_log_lock);
        return 0;
    }

    g_current_level = level;
    g_initialized = 1;
    
    pthread_mutex_unlock(&g_log_lock);
    
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
    return g_current_level;
}