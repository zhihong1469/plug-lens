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

// 全局日志控制块
static log_level_t g_current_level = LOG_LEVEL_INFO;
static int g_initialized = 0;
static int g_is_daemon = 0;
static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

// ===================== 【修复】纯系统调用创建目录（无shell依赖，嵌入式专用） =====================
static int log_mkdir(const char *dir)
{
    char tmp[256];
    char *p = NULL;
    size_t len;

    // 空路径检查
    if (!dir || strlen(dir) >= sizeof(tmp)) {
        printf("[LOG ERROR] Path too long or NULL\n");
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);

    // 去掉末尾 /
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    // 递归创建目录
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

    // 创建最后一级目录
    if (access(tmp, F_OK) != 0) {
        if (mkdir(tmp, 0755) == -1 && errno != EEXIST) {
            printf("[LOG ERROR] mkdir %s failed: %s\n", tmp, strerror(errno));
            return -1;
        }
    }

    printf("[LOG] Create dir success: %s\n", tmp);
    return 0;
}

// 解析文件路径，创建所在目录
static int log_create_dir_by_file(const char *file_path)
{
    char path[256];
    char *last_slash;

    if (!file_path) return -1;
    strncpy(path, file_path, sizeof(path)-1);

    // 查找最后一个 /，截取目录
    last_slash = strrchr(path, '/');
    if (!last_slash) return -1;
    *last_slash = '\0';

    return log_mkdir(path);
}

// 日志滚动（修复版）
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

int log_init(log_level_t level)
{
    pthread_mutex_lock(&g_log_lock);

    if (g_initialized) {
        pthread_mutex_unlock(&g_log_lock);
        return 0;
    }

    // 【关键】检查SD卡是否挂载
    if (access("/mnt/sdcard", F_OK) != 0) {
        printf("[LOG FATAL] SD卡未挂载！请执行: mount /dev/mmcblk0p1 /mnt/sdcard\n");
        pthread_mutex_unlock(&g_log_lock);
        return -1;
    }

    // 自动创建日志目录
    if (log_create_dir_by_file(LOG_FILE_PATH) != 0) {
        printf("[LOG FATAL] 创建日志目录失败\n");
        pthread_mutex_unlock(&g_log_lock);
        return -1;
    }

    // 打开日志文件
    g_log_file = fopen(LOG_FILE_PATH, "a+");
    if (!g_log_file) {
        printf("[LOG FATAL] 打开日志文件失败: %s, error: %s\n", LOG_FILE_PATH, strerror(errno));
        pthread_mutex_unlock(&g_log_lock);
        return -1;
    }

    g_current_level = level;
    g_initialized = 1;
    g_is_daemon = 0;

    printf("[LOG] 初始化成功: %s\n", LOG_FILE_PATH);

    pthread_mutex_unlock(&g_log_lock);
    return 0;
}

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

void log_printf(const char *fmt, ...)
{
    if (!g_initialized) return;

    pthread_mutex_lock(&g_log_lock);
    log_rotate();

    va_list ap;
    va_start(ap, fmt);

    if (g_is_daemon) {
        if (g_log_file) {
            vfprintf(g_log_file, fmt, ap);
            fflush(g_log_file);
        }
    } else {
        if (g_log_file) {
            vfprintf(g_log_file, fmt, ap);
            fflush(g_log_file);
        }
        vprintf(fmt, ap);
    }

    va_end(ap);
    pthread_mutex_unlock(&g_log_lock);
}