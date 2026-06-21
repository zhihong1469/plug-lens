/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_base.h
 * @brief   Image Processing Universal Abstract Base Class
 * @details C-OOP polymorphic design for cross-platform image processing.
 *          Provides unified abstract interface for format conversion, encoding, compression.
 *          Supports software (libyuv/libjpeg-turbo/openh264) and hardware (RGA/MPP) backends.
 *
 *          Architecture (方案C - 混合方案):
 *          - 转换/缩放: 使用单例共享（img_proc_convert_create）
 *          - 编解码: 独立创建（img_proc_codec_create）
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v2.0.0
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
    IMG_PROC_ERR_ENCODE  = -4,   /**< Encoding/compression failure */
    IMG_PROC_ERR_NO_MEM  = -5,   /**< Memory allocation failure */
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
    IMG_FORMAT_BGR888 = 2,   /**< BGR888 24-bit (OpenCV compatible) */
    IMG_FORMAT_NV12   = 3,   /**< NV12 semi-planar (MPP encoder input) */
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
 * Image Processing Type (for factory selection)
 * ========================================================================== */

/**
 * @brief   Image processing capability type
 * @details Used by factory to select appropriate backend ops table
 */
typedef enum {
    IMG_PROC_TYPE_CONVERT = 0,  /**< Format conversion + resize (ai_model_mnn uses) */
    IMG_PROC_TYPE_CODEC   = 1,  /**< Encoding + JPEG (net_push_srv uses) */
    IMG_PROC_TYPE_ALL     = 2   /**< All capabilities (legacy compatibility) */
} img_proc_type_t;

/* ==========================================================================
 * Sub-operation Tables (for interface segregation)
 * ========================================================================== */

/**
 * @brief   H.264 encoder handle (forward declaration for sub-ops)
 */
typedef void* h264_encoder_t;

/**
 * @brief   Format conversion operations
 * @details Subset of img_proc_ops for format conversion
 */
typedef struct img_proc_convert_ops {
    img_proc_err_t (*init)(void *user_data);
    img_proc_err_t (*deinit)(void *user_data);
    img_proc_err_t (*yuyv_to_rgb)(void *user_data, const uint8_t *yuyv, uint8_t *rgb);
    img_proc_err_t (*yuyv_to_nv12)(void *user_data, const uint8_t *yuyv, uint8_t *nv12);
    img_proc_err_t (*yuyv_to_i420)(void *user_data, const uint8_t *yuyv, uint8_t *i420);
    img_proc_err_t (*mjpeg_to_rgb)(void *user_data, const uint8_t *mjpeg, int mjpeg_len, uint8_t *rgb);
    img_proc_err_t (*rgb_resize)(void *user_data, const uint8_t *src, int src_w, int src_h,
                                 uint8_t *dst, int dst_w, int dst_h);
} img_proc_convert_ops_t;

/**
 * @brief   Codec operations (H.264 + JPEG)
 * @details Subset of img_proc_ops for encoding
 */
typedef struct img_proc_codec_ops {
    img_proc_err_t (*init)(void *user_data);
    img_proc_err_t (*deinit)(void *user_data);
    h264_encoder_t (*h264_encoder_create)(void *user_data, const h264_enc_config_t *cfg);
    img_proc_err_t (*yuyv_to_h264)(void *user_data, h264_encoder_t enc,
                                   const uint8_t *yuyv, int yuyv_len,
                                   uint8_t *h264, int *h264_len);
    img_proc_err_t (*h264_encoder_get_sps_pps)(void *user_data, h264_encoder_t enc,
                                               uint8_t *sps_pps, int *len);
    void (*h264_encoder_destroy)(void *user_data, h264_encoder_t enc);
    img_proc_err_t (*rgb_to_jpeg)(void *user_data, const uint8_t *rgb,
                                  uint8_t *jpeg, size_t *jpeg_len);
} img_proc_codec_ops_t;

/**
 * @brief   Drawing operations
 */
typedef struct img_proc_draw_ops {
    void (*bgr_draw_rect)(void *user_data, uint8_t *bgr, int width, int height,
                          int x, int y, int w, int h, uint32_t color, int thickness);
} img_proc_draw_ops_t;

/* ==========================================================================
 * Unified Operation Table (backward compatible)
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
    img_proc_type_t       type;     /**< Processing type */
    void                 *user_data; /**< Backend private data */
    bool                  is_singleton; /**< Singleton flag: true=global shared, don't destroy */
} img_proc_handle_t;

/**
 * @brief   Image processing virtual function table (full capabilities)
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

    /* Sub-operation tables (new in v2.0.0) */
    const img_proc_convert_ops_t *convert_ops;
    const img_proc_codec_ops_t   *codec_ops;
    const img_proc_draw_ops_t    *draw_ops;
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
                                    const img_proc_ops_t *ops,
                                    img_proc_type_t type);

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
 * Sub-operation Accessors (for v2.0.0 interface segregation)
 * ========================================================================== */

/**
 * @brief   Get convert operations from handle
 * @param   handle  Instance handle
 * @return  Convert ops table or NULL if not supported
 */
const img_proc_convert_ops_t *img_proc_get_convert_ops(img_proc_handle_t *handle);

/**
 * @brief   Get codec operations from handle
 * @param   handle  Instance handle
 * @return  Codec ops table or NULL if not supported
 */
const img_proc_codec_ops_t *img_proc_get_codec_ops(img_proc_handle_t *handle);

/**
 * @brief   Get draw operations from handle
 * @param   handle  Instance handle
 * @return  Draw ops table or NULL if not supported
 */
const img_proc_draw_ops_t *img_proc_get_draw_ops(img_proc_handle_t *handle);

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
