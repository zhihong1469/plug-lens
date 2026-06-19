/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_base.h
 * @brief   Image Processing Universal Abstract Base Class
 * @details C-OOP polymorphic design for cross-platform image processing.
 *          Provides unified abstract interface for format conversion, encoding, compression.
 *          Supports software (libyuv/libjpeg-turbo/openh264) and hardware (RGA/MPP) backends.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All functions are not thread-safe unless marked specially.
 *          2. Strict lifecycle: create → init → process → deinit → destroy.
 *          3. Factory pattern hides implementation details from caller.
 */

#ifndef __IMG_PROC_BASE_H__
#define __IMG_PROC_BASE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Error Code Definitions
 * ========================================================================== */

/**
 * @brief   Image processing universal return error code
 */
typedef enum {
    IMG_PROC_OK           =  0,   /**< Operation succeeded */
    IMG_PROC_ERR_PARAM    = -1,   /**< Invalid input parameter */
    IMG_PROC_ERR_INIT     = -2,   /**< Module initialization failure */
    IMG_PROC_ERR_CONVERT  = -3,   /**< Format conversion failure */
    IMG_PROC_ERR_ENCODE   = -4,   /**< Encoding/compression failure */
    IMG_PROC_ERR_NO_MEM   = -5,   /**< Memory allocation failure */
    IMG_PROC_ERR_UNSUPPORTED = -6 /**< Unsupported format/operation */
} img_proc_err_t;

/* ==========================================================================
 * Image Format Definitions
 * ========================================================================== */

/**
 * @brief   Supported image formats for processing
 */
typedef enum {
    IMG_FORMAT_YUYV   = 0,    /**< YUYV 4:2:2 interleaved (camera default) */
    IMG_FORMAT_RGB888 = 1,    /**< RGB888 24-bit (AI model input) */
    IMG_FORMAT_BGR888 = 2,    /**< BGR888 24-bit (OpenCV compatible) */
    IMG_FORMAT_NV12   = 3,    /**< NV12 semi-planar (MPP encoder input) */
    IMG_FORMAT_I420   = 4,    /**< I420 planar (openh264 encoder input) */
    IMG_FORMAT_MJPEG  = 5,    /**< MJPEG compressed (some cameras) */
    IMG_FORMAT_JPEG   = 6,    /**< JPEG compressed (storage) */
    IMG_FORMAT_MAX    = 7     /**< Maximum format index */
} img_format_t;

/* ==========================================================================
 * Configuration Structures
 * ========================================================================== */

/**
 * @brief   Image processing configuration
 * @details Shared configuration for all processing backends
 */
typedef struct {
    int     width;          /**< Input image width (pixels) */
    int     height;         /**< Input image height (pixels) */
    int     fps;            /**< Frame rate for video encoding */
    int     bitrate;        /**< Bitrate for H.264 encoding (kbps) */
    int     gop;            /**< GOP size for H.264 encoding */
    int     jpeg_quality;   /**< JPEG compression quality (1-100) */
} img_proc_config_t;

/**
 * @brief   H.264 encoder configuration
 * @details Reuse img_joint.h definition for compatibility
 * @note    Include img_joint.h for detailed definition
 */
typedef struct {
    int     width;          /**< Video width (MUST be even) */
    int     height;         /**< Video height (MUST be even) */
    int     fps;            /**< Frame rate */
    int     bitrate;        /**< Bitrate in kbps */
    int     gop;            /**< GOP size (I-frame interval) */
    bool    use_cpu_core;   /**< CPU encoding flag (true for i.MX6ULL) */
} h264_enc_config_t;

/* ==========================================================================
 * Base Class Core Structures
 * ========================================================================== */

/**
 * @brief   Forward declaration of operation table
 */
typedef struct img_proc_ops img_proc_ops_t;

/**
 * @brief   Image processing base class handle
 * @details Opaque handle for upper layer usage
 */
typedef struct {
    const img_proc_ops_t *ops;      /**< Polymorphic operation table */
    img_proc_config_t     config;   /**< Configuration copy */
    void                 *user_data; /**< Backend private data */
} img_proc_handle_t;

/**
 * @brief   H.264 encoder handle (opaque)
 * @details Reuse img_joint.h definition: typedef void* h264_encoder_t
 */
typedef void* h264_encoder_t;

/**
 * @brief   Image processing virtual function table
 * @details Mandatory interfaces for all backends (software/hardware)
 */
struct img_proc_ops {
    /* Lifecycle management */
    img_proc_err_t (*init)(img_proc_handle_t *handle);
    img_proc_err_t (*deinit)(img_proc_handle_t *handle);
    
    /* Format conversion */
    img_proc_err_t (*yuyv_to_rgb)(img_proc_handle_t *handle,
                                   const uint8_t *yuyv, uint8_t *rgb);
    img_proc_err_t (*yuyv_to_nv12)(img_proc_handle_t *handle,
                                    const uint8_t *yuyv, uint8_t *nv12);
    img_proc_err_t (*yuyv_to_i420)(img_proc_handle_t *handle,
                                    const uint8_t *yuyv, uint8_t *i420);
    img_proc_err_t (*mjpeg_to_rgb)(img_proc_handle_t *handle,
                                    const uint8_t *mjpeg, int mjpeg_len,
                                    uint8_t *rgb);
    
    /* Image resize */
    img_proc_err_t (*rgb_resize)(img_proc_handle_t *handle,
                                  const uint8_t *src_rgb, int src_w, int src_h,
                                  uint8_t *dst_rgb, int dst_w, int dst_h);
    
    /* JPEG compression */
    img_proc_err_t (*rgb_to_jpeg)(img_proc_handle_t *handle,
                                   const uint8_t *rgb, uint8_t *jpeg,
                                   size_t *jpeg_len);
    
    /* H.264 encoding */
    h264_encoder_t (*h264_encoder_create)(img_proc_handle_t *handle,
                                           const h264_enc_config_t *cfg);
    img_proc_err_t (*yuyv_to_h264)(img_proc_handle_t *handle,
                                   h264_encoder_t encoder,
                                   const uint8_t *yuyv, int yuyv_len,
                                   uint8_t *h264, int *h264_len);
    img_proc_err_t (*h264_encoder_get_sps_pps)(img_proc_handle_t *handle,
                                               h264_encoder_t encoder,
                                               uint8_t *sps_pps, int *len);
    void (*h264_encoder_destroy)(img_proc_handle_t *handle,
                                 h264_encoder_t encoder);
    
    /* Drawing utilities */
    void (*bgr_draw_rect)(img_proc_handle_t *handle,
                          uint8_t *bgr_data, int width, int height,
                          int x, int y, int w, int h,
                          uint32_t color, int thickness);
};

/* ==========================================================================
 * Public Universal API
 * ========================================================================== */

/**
 * @brief   Create image processing instance
 * @param   config  Pointer to configuration
 * @param   ops     Pointer to backend operation table
 * @return  Valid handle on success, NULL on failure
 */
img_proc_handle_t *img_proc_create(const img_proc_config_t *config,
                                    const img_proc_ops_t *ops);

/**
 * @brief   Destroy image processing instance
 * @param   handle  Instance handle
 */
void img_proc_destroy(img_proc_handle_t *handle);

/**
 * @brief   Initialize image processing module
 * @param   handle  Instance handle
 * @return  img_proc_err_t
 */
img_proc_err_t img_proc_init(img_proc_handle_t *handle);

/**
 * @brief   Deinitialize image processing module
 * @param   handle  Instance handle
 * @return  img_proc_err_t
 */
img_proc_err_t img_proc_deinit(img_proc_handle_t *handle);

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * @brief   Calculate buffer size for given format
 * @param   format  Image format
 * @param   width   Image width
 * @param   height  Image height
 * @return  Buffer size in bytes
 */
size_t img_proc_calc_buffer_size(img_format_t format, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* __IMG_PROC_BASE_H__ */