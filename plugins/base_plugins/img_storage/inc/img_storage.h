/**
 * @file    img_storage.h
 * @brief   Universal Image Storage Service Module
 * @details Thread-safe image storage module with TurboJPEG encoding support,
 *          no SD card dependency, compatible with local/NFS/network mount paths.
 *          Core features: RGB raw data I/O, JPEG compression, auto file cleanup, disk sync.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @relies  https://github.com/libjpeg-turbo/libjpeg-turbo
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All public APIs are thread-safe (mutex protected).
 *          2. Call sequence: init → save/read → deinit.
 *          3. Works without SD card, uses configurable mount path.
 */
#ifndef IMG_STORAGE_H
#define IMG_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vision_ai_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ========================== Path Configuration ==========================
/** Root path for image storage (mount point compatible) */
#define IMG_STORAGE_ROOT_PATH           CONFIG_SD_STORAGE_ROOT_PATH
/** Working directory for face capture images */
#define IMG_STORAGE_DIR                 CONFIG_SD_STORAGE_DIR
/** Maximum number of cached image files (auto cleanup old files) */
#define IMG_MAX_CAPTURE_FILES           200000

// ========================== Image Parameters ==========================
/** Input image width (global video configuration) */
#define IMG_INPUT_WIDTH                 GLOBAL_VIDEO_WIDTH
/** Input image height (global video configuration) */
#define IMG_INPUT_HEIGHT                GLOBAL_VIDEO_HEIGHT
/** Total size of RGB888 image buffer (width * height * 3 channels) */
#define IMG_RGB_IMAGE_SIZE              (IMG_INPUT_WIDTH * IMG_INPUT_HEIGHT * 3)

// ========================== Error Code Definition ==========================
/**
 * @brief   Image storage module return codes
 */
typedef enum {
    IMG_STORAGE_OK           =  0,   /**< Operation succeeded */
    IMG_STORAGE_ERR_PARAM    = -1,   /**< Invalid input parameter */
    IMG_STORAGE_ERR_NOT_INIT = -2,   /**< Module not initialized */
    IMG_STORAGE_ERR_FILE     = -3,   /**< File operation failed */
    IMG_STORAGE_ERR_NO_MEM   = -4    /**< Insufficient memory/buffer size */
} ImgStorage_Error_t;

/**
 * @brief   Opaque handle for image storage module instance
 * @details External code must use this pointer only, no direct member access.
 */
typedef struct ImgStorage ImgStorage_t;

/**
 * @brief   Initialize image storage module
 * @details Allocate resources, init mutex, create TurboJPEG encoder and working directory.
 *          No SD card dependency, supports all Linux mount paths.
 * @return  ImgStorage_t*  Module handle on success; NULL on failure.
 *
 * @pre     None (first API to call)
 * @post    Module is initialized and ready for I/O operations
 * @note    Single instance only supported
 * @warning Do not call other APIs before initialization
 * @thread_safety No
 * @example Usage demo:
 * @code
 * ImgStorage_t *storage = img_storage_init();
 * if (storage == NULL) {
 *     // Handle initialization failure
 * }
 * @endcode
 */
ImgStorage_t *img_storage_init(void);

/**
 * @brief   De-initialize image storage module
 * @details Release all resources: mutex, TurboJPEG handle, dynamic memory.
 * @param   self  Pointer to image storage module handle
 * @return  void
 *
 * @pre     Module must be initialized successfully
 * @post    All resources released, handle becomes invalid
 * @note    Must be called at program exit to avoid resource leaks
 * @warning Do not access the handle after deinitialization
 * @thread_safety No
 */
void img_storage_deinit(ImgStorage_t *self);

/**
 * @brief   Save raw RGB888 image data to file
 * @details Thread-safe write, auto cleanup old files, disk sync for NFS real-time preview.
 * @param   self      Pointer to module handle
 * @param   rgb_buf   Pointer to RGB image data buffer (size = IMG_RGB_IMAGE_SIZE)
 * @return  ImgStorage_Error_t  Status code
 *
 * @pre     Module initialized, rgb_buf not NULL
 * @post    RGB file saved to working directory
 * @note    File name auto-generated with timestamp
 * @thread_safety Yes (mutex protected)
 */
int img_storage_save_rgb(ImgStorage_t *self, const uint8_t *rgb_buf);

/**
 * @brief   Read raw RGB888 image data from file
 * @details Thread-safe read operation from storage directory.
 * @param   self      Pointer to module handle
 * @param   filename  Target RGB file name (not full path)
 * @param   out_buf   Output buffer for RGB data
 * @param   buf_size  Size of output buffer (must >= IMG_RGB_IMAGE_SIZE)
 * @return  ImgStorage_Error_t  Status code
 *
 * @pre     Module initialized, input parameters valid
 * @post    RGB data loaded into output buffer
 * @thread_safety Yes (mutex protected)
 */
int img_storage_read_rgb(ImgStorage_t *self, const char *filename, uint8_t *out_buf, size_t buf_size);

/**
 * @brief   Compress and save image as standard JPEG file
 * @details Uses TurboJPEG encoder (shared with stream module), fast compression,
 *          auto file permission and disk sync for network sharing.
 * @param   self      Pointer to module handle
 * @param   rgb_buf   Pointer to RGB input image buffer
 * @return  ImgStorage_Error_t  Status code
 *
 * @pre     Module initialized, rgb_buf not NULL
 * @post    JPEG file saved with 0666 permissions
 * @note    JPEG quality fixed at 50 (balanced speed/size)
 * @thread_safety Yes (mutex protected)
 */
int img_storage_save_jpeg(ImgStorage_t *self, const uint8_t *rgb_buf);

/**
 * @brief   Get free space of storage directory
 * @details Query disk free space in MB using system df command.
 * @return  long long  Free space in MB; negative value on failure
 *
 * @pre     None (can be called without initialization)
 * @post    No system state changes
 * @note    Works for all Linux file systems
 * @thread_safety Yes
 */
long long img_storage_get_free_space_mb(void);

#ifdef __cplusplus
}
#endif

#endif