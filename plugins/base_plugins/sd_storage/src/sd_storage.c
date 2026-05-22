#include "sd_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

// ==============================================
// 私有结构体（仅内部可见，封装实现）
// ==============================================
struct SdStorage {
    bool            is_init;        // 初始化标记
    pthread_mutex_t mutex;          // 线程安全互斥锁
    char            save_path[128]; // 存储完整路径
};

// 全局优雅退出标记
bool g_sd_storage_exit_flag = false;

// ==============================================
// 静态私有工具函数（单一职责，static封装）
// ==============================================
/**
 * @brief  创建存储目录（支持递归）
 */
static int SdStorage_CreateDir(const char *path) {
    if (access(path, F_OK) == 0) return 0;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);
    return system(cmd);
}

/**
 * @brief  生成带时间戳的文件名
 */
static void SdStorage_GenFileName(char *name, size_t len) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(name, len, "face_%04d%02d%02d_%02d%02d%02d.rgb",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/**
 * @brief  清理最旧的文件（循环存储核心）
 */
static void SdStorage_CleanOldFiles(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    char oldest_file[256] = {0};
    int file_count = 0;

    // 遍历统计文件，找到最旧文件
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".rgb") == NULL) continue;

        file_count++;
        // 记录第一个文件为默认最旧
        if (strlen(oldest_file) == 0) {
            snprintf(oldest_file, sizeof(oldest_file), "%s/%s", path, entry->d_name);
        }
    }
    closedir(dir);

    // 超过最大数量，删除最旧
    if (file_count >= SD_MAX_SAVE_FILES && strlen(oldest_file) > 0) {
        remove(oldest_file);
    }
}

// ==============================================
// 公共API实现
// ==============================================
SdStorage_t *SdStorage_Init(void) {
    // 优雅退出检查
    if (g_sd_storage_exit_flag) return NULL;

    // 分配模块对象（可替换为静态内存池，适配你的框架）
    SdStorage_t *self = (SdStorage_t *)calloc(1, sizeof(SdStorage_t));
    if (!self) return NULL;

    // 初始化互斥锁
    if (pthread_mutex_init(&self->mutex, NULL) != 0) {
        free(self);
        return NULL;
    }

    // 创建存储目录
    if (SdStorage_CreateDir(SD_STORAGE_PATH) != 0) {
        pthread_mutex_destroy(&self->mutex);
        free(self);
        return NULL;
    }

    snprintf(self->save_path, sizeof(self->save_path), "%s", SD_STORAGE_PATH);
    self->is_init = true;
    return self;
}

int SdStorage_SaveRgbImage(SdStorage_t *self, const uint8_t *rgb_buf) {
    // 防御性编程：参数/状态检查
    if (!self || !self->is_init || !rgb_buf || g_sd_storage_exit_flag) {
        return -1;
    }

    // 线程安全：加锁
    if (pthread_mutex_lock(&self->mutex) != 0) {
        return -2;
    }

    int ret = -1;
    char file_name[64] = {0};
    char full_path[256] = {0};
    FILE *fp = NULL;

    // 1. 生成文件名
    SdStorage_GenFileName(file_name, sizeof(file_name));
    snprintf(full_path, sizeof(full_path), "%s/%s", self->save_path, file_name);

    // 2. 循环清理旧文件
    SdStorage_CleanOldFiles(self->save_path);

    // 3. 写入RGB原始数据（直接使用上层静态内存）
    fp = fopen(full_path, "wb");
    if (!fp) goto exit;

    size_t write_size = fwrite(rgb_buf, 1, SD_IMAGE_WIDTH * SD_IMAGE_HEIGHT * SD_RGB_BPP, fp);
    if (write_size != SD_IMAGE_WIDTH * SD_IMAGE_HEIGHT * SD_RGB_BPP) {
        fclose(fp);
        remove(full_path); // 写入失败，删除损坏文件
        goto exit;
    }

    fflush(fp);
    fsync(fileno(fp)); // 强制刷入SD卡，保证数据完整性
    ret = 0;

exit:
    // 统一资源清理（goto规范用法）
    if (fp) fclose(fp);
    pthread_mutex_unlock(&self->mutex);
    return ret;
}

void SdStorage_Deinit(SdStorage_t **self) {
    if (!self || !*self) return;

    SdStorage_t *p = *self;
    g_sd_storage_exit_flag = true; // 标记退出

    // 线程安全：加锁
    pthread_mutex_lock(&p->mutex);

    // 销毁资源
    pthread_mutex_unlock(&p->mutex);
    pthread_mutex_destroy(&p->mutex);
    free(p);

    *self = NULL; // 指针置空，防止野指针
}