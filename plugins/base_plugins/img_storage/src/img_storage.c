/**
 * @file    img_storage.c
 * @brief   Universal Image Storage Service Implementation
 * @details Internal implementation:
 *          - Thread-safe operations with pthread mutex
 *          - TurboJPEG hardware-accelerated JPEG encoding
 *          - Recursive directory creation and automatic old file cleanup
 *          - fsync disk synchronization for NFS/network real-time preview
 *          - No SD card dependency, pure Linux file I/O
 *          - Static memory management at initialization only
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @relies  https://github.com/libjpeg-turbo/libjpeg-turbo
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#ifndef USE_SD
#define USE_SD 0
#endif
/* SPDX-License-Identifier: MIT */
#include "img_storage.h"
#include "log.h"
// Reuse TurboJPEG library from project stream module
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

/**
 * @brief   Private structure for image storage instance
 * @details Core context for storage module, contains all runtime resources.
 * @note    External code cannot access members directly (opaque pointer).
 */
struct ImgStorage {
    bool            is_initialized;  /**< Module initialization flag */
    pthread_mutex_t mutex;           /**< Thread safety mutex for I/O operations */
    char            work_dir[128];   /**< Working directory path */
    tjhandle        tj_handle;       /**< TurboJPEG compressor handle */
    int             jpeg_quality;    /**< JPEG encoding quality (1-100) */
};

// ==============================================
// Private Static Helper Functions
// ==============================================
/**
 * @brief   Recursively create directory
 * @details Uses system mkdir command for cross-platform compatibility.
 * @param   path  Target directory path
 * @return  0 on success; non-zero on failure
 */
static int img_storage_mkdir(const char *path) {
    if (access(path, F_OK) == 0) return 0;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);
    int ret = system(cmd);
    if (ret != 0) LOG_E("[IMG_STORAGE] Failed to create directory: %s", path);
    return ret;
}

/**
 * @brief   Generate time-stamped RGB file name
 * @details File format: face_YYYYMMDD_HHMMSS.rgb
 * @param   name  Output buffer for file name
 * @param   len   Size of name buffer
 * @return  void
 */
static void img_storage_gen_rgb_filename(char *name, size_t len) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(name, len, "face_%04d%02d%02d_%02d%02d%02d.rgb",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/**
 * @brief   Generate time-stamped JPEG file name
 * @details File format: face_YYYYMMDD_HHMMSS.jpg
 * @param   name  Output buffer for file name
 * @param   len   Size of name buffer
 * @return  void
 */
static void img_storage_gen_jpeg_filename(char *name, size_t len) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(name, len, "face_%04d%02d%02d_%02d%02d%02d.jpg",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/**
 * @brief   Auto clean up old image files
 * @details Delete oldest RGB/JPG file when file count exceeds maximum limit.
 *          Protects disk space from overflow.
 * @param   dir_path  Target directory to clean up
 * @return  void
 */
static void img_storage_clean_old_files(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *ent;
    struct stat st;
    char oldest_file[256] = {0};
    time_t oldest_time = (time_t)-1;
    int file_cnt = 0;
    char full_path[256];

    while ((ent = readdir(dir)) != NULL) {
        // Filter RGB and JPG files only
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

    // Delete oldest file if over max limit
    if (file_cnt >= IMG_MAX_CAPTURE_FILES && strlen(oldest_file) > 0) {
        remove(oldest_file);
        LOG_I("[IMG_STORAGE] Max limit reached, deleted old file: %s", oldest_file);
    }
}

// ==============================================
// Public API Implementation (Lifecycle Order)
// ==============================================
/**
 * @copydoc img_storage_init
 */
ImgStorage_t *img_storage_init(void)
{
    // Allocate module instance memory
    ImgStorage_t *self = (ImgStorage_t *)mem_calloc(1, sizeof(ImgStorage_t));
    if (!self) return NULL;

    // Initialize thread mutex for thread safety
    if (pthread_mutex_init(&self->mutex, NULL) != 0) {
        mem_free(self);
        return NULL;
    }

    // Initialize TurboJPEG compressor (core encoding component)
    self->tj_handle = tjInitCompress();
    if (!self->tj_handle) {
        pthread_mutex_destroy(&self->mutex);
        mem_free(self);
        return NULL;
    }

    // Create working directory (fail fast if directory creation fails)
    if (img_storage_mkdir(IMG_STORAGE_DIR) != 0) {
        tjDestroy(self->tj_handle);
        pthread_mutex_destroy(&self->mutex);
        mem_free(self);
        return NULL;
    }

    // Initialize runtime parameters
    strncpy(self->work_dir, IMG_STORAGE_DIR, sizeof(self->work_dir)-1);
    self->jpeg_quality = 50;
    self->is_initialized = true;

    LOG_I("[IMG_STORAGE] Initialization completed");
    return self;
}

/**
 * @copydoc img_storage_deinit
 */
void img_storage_deinit(ImgStorage_t *self) {
    if (!self || !self->is_initialized) return;

    pthread_mutex_lock(&self->mutex);
    // Release TurboJPEG encoder resource
    if (self->tj_handle) {
        tjDestroy(self->tj_handle);
        self->tj_handle = NULL;
    }
    pthread_mutex_unlock(&self->mutex);

    // Release system resources
    pthread_mutex_destroy(&self->mutex);
    mem_free(self);

    LOG_I("[IMG_STORAGE] De-initialization completed");
}

/**
 * @copydoc img_storage_save_rgb
 */
int img_storage_save_rgb(ImgStorage_t *self, const uint8_t *rgb_buf) {
    if (!self || !self->is_initialized || !rgb_buf) return IMG_STORAGE_ERR_PARAM;

    pthread_mutex_lock(&self->mutex);
    int ret = IMG_STORAGE_ERR_FILE;
    char filename[64] = {0};
    char fullpath[256] = {0};
    FILE *fp = NULL;

    // Generate unique file name
    img_storage_gen_rgb_filename(filename, sizeof(filename));
    snprintf(fullpath, sizeof(fullpath)-1, "%s/%s", self->work_dir, filename);

    // Clean up old files before writing
    img_storage_clean_old_files(self->work_dir);

    // Write raw RGB data
    fp = fopen(fullpath, "wb");
    if (!fp) goto exit;

    size_t w_size = fwrite(rgb_buf, 1, IMG_RGB_IMAGE_SIZE, fp);
    if (w_size != IMG_RGB_IMAGE_SIZE) {
        fclose(fp);
        remove(fullpath);
        goto exit;
    }

    // Force disk sync for NFS real-time visibility
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    ret = IMG_STORAGE_OK;
    LOG_D("[IMG_STORAGE] RGB saved successfully: %s", filename);

exit:
    pthread_mutex_unlock(&self->mutex);
    return ret;
}

/**
 * @copydoc img_storage_read_rgb
 */
int img_storage_read_rgb(ImgStorage_t *self, const char *filename, uint8_t *out_buf, size_t buf_size) {
    if (!self || !self->is_initialized || !filename || !out_buf) return IMG_STORAGE_ERR_PARAM;
    if (buf_size < IMG_RGB_IMAGE_SIZE) return IMG_STORAGE_ERR_NO_MEM;

    pthread_mutex_lock(&self->mutex);
    int ret = IMG_STORAGE_ERR_FILE;
    char fullpath[256] = {0};
    FILE *fp = NULL;

    snprintf(fullpath, sizeof(fullpath)-1, "%s/%s", self->work_dir, filename);
    fp = fopen(fullpath, "rb");
    if (!fp) goto exit;

    size_t r_size = fread(out_buf, 1, IMG_RGB_IMAGE_SIZE, fp);
    fclose(fp);
    ret = (r_size == IMG_RGB_IMAGE_SIZE) ? IMG_STORAGE_OK : IMG_STORAGE_ERR_FILE;

exit:
    pthread_mutex_unlock(&self->mutex);
    return ret;
}

/**
 * @copydoc img_storage_get_free_space_mb
 */
long long img_storage_get_free_space_mb(void) {
    uint64_t free_bytes = 0;
    FILE *fp = popen("df -B1 /mnt/test | tail -1 | awk '{print $4}'", "r");
    if (fp) {
        fscanf(fp, "%llu", &free_bytes);
        pclose(fp);
    }
    return (long long)(free_bytes / 1024 / 1024);
}

/**
 * @copydoc img_storage_save_jpeg
 */
int img_storage_save_jpeg(ImgStorage_t *self, const uint8_t *rgb_buf) {
    if (!self || !self->is_initialized || !rgb_buf) return IMG_STORAGE_ERR_PARAM;

    pthread_mutex_lock(&self->mutex);
    int ret = IMG_STORAGE_ERR_FILE;
    char filename[64] = {0};
    char fullpath[256] = {0};
    FILE *fp = NULL;
    uint8_t *jpeg_buf = NULL;
    unsigned long jpeg_size = 0;

    // Generate JPEG file name
    img_storage_gen_jpeg_filename(filename, sizeof(filename));
    snprintf(fullpath, sizeof(fullpath)-1, "%s/%s", self->work_dir, filename);

    // Auto cleanup old files
    img_storage_clean_old_files(self->work_dir);

    // TurboJPEG compression (same logic as stream encoder)
    int tj_ret = tjCompress2(self->tj_handle,
                          (unsigned char *)rgb_buf,
                          IMG_INPUT_WIDTH,
                          IMG_INPUT_WIDTH * 3,
                          IMG_INPUT_HEIGHT,
                          TJPF_BGR,
                          &jpeg_buf,
                          &jpeg_size,
                          TJSAMP_420,
                          self->jpeg_quality,
                          TJFLAG_FASTDCT);

    if (tj_ret != 0 || jpeg_size == 0) {
        LOG_E("[IMG_STORAGE] JPEG encode failed: %s", filename);
        goto exit;
    }

    // Write JPEG data to file
    fp = fopen(fullpath, "wb");
    if (!fp) goto exit;

    size_t w_size = fwrite(jpeg_buf, 1, jpeg_size, fp);
    if (w_size != jpeg_size) {
        fclose(fp);
        remove(fullpath);
        goto exit;
    }
    // Set global read/write permission for network sharing
    fchmod(fileno(fp), 0666);
    // Disk sync for NFS real-time preview
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    ret = IMG_STORAGE_OK;
    LOG_I("[IMG_STORAGE] JPG saved: %s | Size:%lu Bytes", filename, jpeg_size);

exit:
    // Release TurboJPEG internal buffer
    if (jpeg_buf) tjFree(jpeg_buf);
    pthread_mutex_unlock(&self->mutex);
    return ret;
}