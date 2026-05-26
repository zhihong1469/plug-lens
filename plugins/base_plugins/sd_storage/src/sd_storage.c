/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           sd_storage.c
 * @brief          SD卡存储服务模块实现
 * @details        1. 线程安全的RGB原始数据读写
 *                 2. 基于TurboJPEG的标准JPEG图片编码存储
 *                 3. 自动清理旧文件 + fsync磁盘同步
 *                 4. 兼容NFS网络挂载实时预览
 * @author         Luo
 * @date           2026
 ******************************************************************************
 */
#include "sd_storage.h"
#include "log.h"
// 复用项目推流模块的TurboJPEG库
#include <turbojpeg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>

// ==============================================
// 私有结构体：保留原有成员 + 新增JPEG编码器句柄
// ==============================================
struct SdStorage {
    bool            is_initialized;
    pthread_mutex_t mutex;
    char            work_dir[128];
    tjhandle        tj_handle;     // TurboJPEG编码器句柄
    int             jpeg_quality;  // JPEG编码质量
};

// ==============================================
// 静态工具函数：100%保留原有逻辑
// ==============================================
/**
 * @brief  递归创建文件夹
 * @param  path: 文件夹路径
 * @return 0成功，负数失败
 */
static int sd_storage_mkdir(const char *path) {
    if (access(path, F_OK) == 0) return 0;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);
    int ret = system(cmd);
    if (ret != 0) LOG_E("[SD_STORAGE] 创建目录失败: %s", path);
    return ret;
}

/**
 * @brief  生成RGB文件名（原有逻辑，完整保留）
 */
static void sd_storage_gen_rgb_filename(char *name, size_t len) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(name, len, "face_%04d%02d%02d_%02d%02d%02d.rgb",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/**
 * @brief  生成JPEG文件名（新增，标准格式）
 */
static void sd_storage_gen_jpeg_filename(char *name, size_t len) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(name, len, "face_%04d%02d%02d_%02d%02d%02d.jpg",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/**
 * @brief  清理旧文件，兼容RGB+JPG（原有逻辑增强）
 */
static void sd_storage_clean_old_files(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *ent;
    struct stat st;
    char oldest_file[256] = {0};
    time_t oldest_time = (time_t)-1;
    int file_cnt = 0;
    char full_path[256];

    while ((ent = readdir(dir)) != NULL) {
        // 同时匹配rgb和jpg格式文件
        if (!strstr(ent->d_name, ".rgb") && !strstr(ent->d_name, ".jpg")) continue;

        snprintf(full_path, sizeof(full_path)-1, "%s/%s", dir_path, ent->d_name);
        if (stat(full_path, &st) == -1) continue;

        file_cnt++;
        if (oldest_time == (time_t)-1 || st.st_mtime < oldest_time) {
            oldest_time = st.st_mtime;
            strncpy(oldest_file, full_path, sizeof(oldest_file)-1);
        }
    }
    closedir(dir);

    if (file_cnt >= SD_MAX_CAPTURE_FILES && strlen(oldest_file) > 0) {
        remove(oldest_file);
        LOG_I("[SD_STORAGE] 超过最大数量，删除旧文件: %s", oldest_file);
    }
}

// ==============================================
// 公共API：原有接口完整保留
// ==============================================
SdStorage_t *SdStorage_Init(void) {
    if (access(SD_STORAGE_ROOT_PATH, F_OK) != 0) {
        LOG_E("[SD_STORAGE] SD卡未挂载，请先mount /dev/mmcblk0p1 /mnt/sdcard");
        return NULL;
    }

    SdStorage_t *self = (SdStorage_t *)mem_calloc(1, sizeof(SdStorage_t));
    if (!self) return NULL;

    if (pthread_mutex_init(&self->mutex, NULL) != 0) {
        mem_free(self);
        return NULL;
    }

    // 初始化TurboJPEG编码器（复用推流库）
    self->tj_handle = tjInitCompress();
    if (!self->tj_handle) {
        LOG_E("[SD_STORAGE] TurboJPEG编码器初始化失败");
        pthread_mutex_destroy(&self->mutex);
        mem_free(self);
        return NULL;
    }

    if (sd_storage_mkdir(SD_STORAGE_DIR) != 0) {
        tjDestroy(self->tj_handle);
        pthread_mutex_destroy(&self->mutex);
        mem_free(self);
        return NULL;
    }

    strncpy(self->work_dir, SD_STORAGE_DIR, sizeof(self->work_dir)-1);
    self->jpeg_quality = 20;
    self->is_initialized = true;

    LOG_I("[SD_STORAGE] 初始化成功，存储目录: %s", SD_STORAGE_DIR);
    return self;
}

void SdStorage_Deinit(SdStorage_t *self) {
    if (!self || !self->is_initialized) return;

    pthread_mutex_lock(&self->mutex);
    // 释放JPEG编码器
    if (self->tj_handle) {
        tjDestroy(self->tj_handle);
        self->tj_handle = NULL;
    }
    pthread_mutex_unlock(&self->mutex);

    pthread_mutex_destroy(&self->mutex);
    mem_free(self);

    LOG_I("[SD_STORAGE] 反初始化完成");
}

int SdStorage_SaveRgb(SdStorage_t *self, const uint8_t *rgb_buf) {
    if (!self || !self->is_initialized || !rgb_buf) return SD_STORAGE_ERR_PARAM;

    pthread_mutex_lock(&self->mutex);
    int ret = SD_STORAGE_ERR_FILE;
    char filename[64] = {0};
    char fullpath[256] = {0};
    FILE *fp = NULL;

    sd_storage_gen_rgb_filename(filename, sizeof(filename));
    snprintf(fullpath, sizeof(fullpath)-1, "%s/%s", self->work_dir, filename);

    sd_storage_clean_old_files(self->work_dir);

    fp = fopen(fullpath, "wb");
    if (!fp) goto exit;

    size_t w_size = fwrite(rgb_buf, 1, SD_RGB_IMAGE_SIZE, fp);
    if (w_size != SD_RGB_IMAGE_SIZE) {
        fclose(fp);
        remove(fullpath);
        goto exit;
    }

    // 强制同步磁盘，NFS实时可见
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    ret = SD_STORAGE_OK;
    LOG_D("[SD_STORAGE] RGB保存成功: %s", filename);

exit:
    pthread_mutex_unlock(&self->mutex);
    return ret;
}

int SdStorage_ReadRgb(SdStorage_t *self, const char *filename, uint8_t *out_buf, size_t buf_size) {
    if (!self || !self->is_initialized || !filename || !out_buf) return SD_STORAGE_ERR_PARAM;
    if (buf_size < SD_RGB_IMAGE_SIZE) return SD_STORAGE_ERR_NO_MEM;

    pthread_mutex_lock(&self->mutex);
    int ret = SD_STORAGE_ERR_FILE;
    char fullpath[256] = {0};
    FILE *fp = NULL;

    snprintf(fullpath, sizeof(fullpath)-1, "%s/%s", self->work_dir, filename);
    fp = fopen(fullpath, "rb");
    if (!fp) goto exit;

    size_t r_size = fread(out_buf, 1, SD_RGB_IMAGE_SIZE, fp);
    fclose(fp);
    ret = (r_size == SD_RGB_IMAGE_SIZE) ? SD_STORAGE_OK : SD_STORAGE_ERR_FILE;

exit:
    pthread_mutex_unlock(&self->mutex);
    return ret;
}

long long SdStorage_GetFreeSpaceMB(void) {
    uint64_t free_bytes = 0;
    FILE *fp = popen("df -B1 /mnt/sdcard | tail -1 | awk '{print $4}'", "r");
    if (fp) {
        fscanf(fp, "%llu", &free_bytes);
        pclose(fp);
    }
    return (long long)(free_bytes / 1024 / 1024);
}

// ==============================================
// 新增API：标准JPEG保存（复用推流编码逻辑）
// ==============================================
int SdStorage_SaveJpeg(SdStorage_t *self, const uint8_t *rgb_buf) {
    if (!self || !self->is_initialized || !rgb_buf) return SD_STORAGE_ERR_PARAM;

    pthread_mutex_lock(&self->mutex);
    int ret = SD_STORAGE_ERR_FILE;
    char filename[64] = {0};
    char fullpath[256] = {0};
    FILE *fp = NULL;
    uint8_t *jpeg_buf = NULL;
    unsigned long jpeg_size = 0;

    // 生成标准JPG文件名
    sd_storage_gen_jpeg_filename(filename, sizeof(filename));
    snprintf(fullpath, sizeof(fullpath)-1, "%s/%s", self->work_dir, filename);

    // 自动清理旧文件
    sd_storage_clean_old_files(self->work_dir);

    // TurboJPEG编码（和net_push_srv.c完全一致）
    int tj_ret = tjCompress2(self->tj_handle,
                          (unsigned char *)rgb_buf,
                          SD_INPUT_WIDTH,
                          SD_INPUT_WIDTH * 3,
                          SD_INPUT_HEIGHT,
                          TJPF_BGR,
                          &jpeg_buf,
                          &jpeg_size,
                          TJSAMP_420,
                          self->jpeg_quality,
                          TJFLAG_FASTDCT);

    if (tj_ret != 0 || jpeg_size == 0) {
        LOG_E("[SD_STORAGE] JPEG编码失败: %s", filename);
        goto exit;
    }

    // 写入文件
    fp = fopen(fullpath, "wb");
    if (!fp) goto exit;

    size_t w_size = fwrite(jpeg_buf, 1, jpeg_size, fp);
    if (w_size != jpeg_size) {
        fclose(fp);
        remove(fullpath);
        goto exit;
    }
    fchmod(fileno(fp), 0666);  // 关键：设置文件为全局可读写删除
    // 磁盘同步，NFS实时可见
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    ret = SD_STORAGE_OK;
    LOG_I("[SD_STORAGE] JPG保存成功: %s | 大小:%lu Bytes", filename, jpeg_size);

exit:
    if (jpeg_buf) tjFree(jpeg_buf);
    pthread_mutex_unlock(&self->mutex);
    return ret;
}