/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc.h
 * @brief   Image processing module for plug-lens Vision AI terminal
 * @details Core capabilities:
 *          1. Unified image buffer management and format definition
 *          2. Modular image preprocessing operations (convert/resize/normalize)
 *          3. Support dynamic implementation registration (pure C/OpenCV)
 *          4. Optimized for AI model input tensor generation
 *          5. Embedded Linux platform compatible with zero dynamic allocation
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. Image buffer memory is managed by caller, no internal allocation
 *          2. Register implementation before using any processing APIs
 *          3. All functions are not thread-safe unless specified
 */
#ifndef __IMG_PROC_H__
#define __IMG_PROC_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Enumeration of supported image formats
 * @details Standard pixel formats for vision capture and AI inference
 */
typedef enum {
    IMG_FMT_YUYV422 = 0,  /**< YUYV 4:2:2 interleaved format (camera default) */
    IMG_FMT_RGB565,       /**< RGB565 16-bit color format */
    IMG_FMT_RGB888,       /**< RGB888 24-bit format (AI model input standard) */
    IMG_FMT_BGR888,       /**< BGR888 24-bit format (OpenCV compatible) */
    IMG_FMT_GRAY8,        /**< 8-bit grayscale format */
    IMG_FMT_MAX           /**< Maximum format index for validity check */
} img_format_t;

/**
 * @brief   Image buffer structure for data management
 * @details Unified container for image data, format and dimension parameters
 * @note    External code must use APIs to operate, avoid direct member modification
 */
typedef struct {
    uint8_t *data;        /**< Pointer to pixel data buffer */
    img_format_t format;  /**< Image pixel format */
    uint32_t width;       /**< Image width in pixels */
    uint32_t height;      /**< Image height in pixels */
    uint32_t stride;      /**< Line stride in bytes (alignment compensation) */
    uint32_t size;        /**< Total data size in bytes */
} img_buf_t;

/**
 * @brief   Image preprocessing operation function pointer set
 * @details Unified interface for format conversion, resizing and normalization
 *          Supports dynamic switching between pure C and OpenCV implementations
 */
typedef struct {
    /**
     * @brief   Convert image between different formats
     * @param   in      Input image buffer (read-only)
     * @param   out     Output image buffer (pre-allocated)
     * @return  0 on success, -1 on invalid parameter/unsupported format
     */
    int (*convert)(const img_buf_t *in, img_buf_t *out);
    
    /**
     * @brief   Resize image using nearest-neighbor interpolation
     * @param   in      Input image buffer (read-only)
     * @param   out     Output image buffer (pre-allocated)
     * @return  0 on success, -1 on invalid parameter/unsupported format
     */
    int (*resize)(const img_buf_t *in, img_buf_t *out);
    
    /**
     * @brief   Normalize RGB image to float tensor for AI inference
     * @param   in          Input RGB888 image buffer
     * @param   out         Output float tensor buffer
     * @param   norm_mean   Normalization mean array (3 channels)
     * @param   norm_std    Normalization standard deviation array (3 channels)
     * @param   layout      Tensor layout: "NHWC" or "NCHW"
     * @return  0 on success, -1 on invalid parameter/unsupported layout
     */
    int (*normalize)(const img_buf_t *in, float *out,
                     const float *norm_mean, const float *norm_std,
                     const char *layout);
} img_ops_t;

/**
 * @brief   Register image preprocessing implementation
 * @param   ops     Pointer to operation function set
 * @return  0 on success, -1 on null pointer input
 *
 * @pre     No implementation registered before
 * @post    Registered operations available for all processing APIs
 * @note    Call once before using any image processing functions
 * @thread_safety No
 */
int img_proc_register(const img_ops_t *ops);

/**
 * @brief   Get registered image preprocessing operations
 * @return  Pointer to registered ops, NULL if not registered
 *
 * @pre     Implementation has been registered via img_proc_register()
 * @thread_safety No
 */
const img_ops_t *img_proc_get_ops(void);

/**
 * @brief   Calculate total image buffer size in bytes
 * @param   fmt     Image format
 * @param   width   Image width
 * @param   height  Image height
 * @return  Calculated buffer size, 0 on invalid format
 *
 * @thread_safety Yes
 */
uint32_t img_proc_calc_size(img_format_t fmt, uint32_t width, uint32_t height);

/**
 * @brief   Initialize image buffer structure (no memory allocation)
 * @param   buf     Image buffer pointer to initialize
 * @param   data    External pixel data pointer
 * @param   fmt     Image format
 * @param   width   Image width
 * @param   height  Image height
 *
 * @pre     Data buffer must be allocated by caller
 * @post    Buffer parameters (stride/size) auto-calculated
 * @note    No internal memory management, caller responsible for data lifetime
 * @thread_safety No
 *
 * @example
 * @code
 * uint8_t pixel_data[320*240*3];
 * img_buf_t buf;
 * img_buf_init(&buf, pixel_data, IMG_FMT_RGB888, 320, 240);
 * @endcode
 */
void img_buf_init(img_buf_t *buf, uint8_t *data, img_format_t fmt,
                  uint32_t width, uint32_t height);

/**
 * @brief   Initialize image processing module with pure C implementation
 * @details Auto-register default pure C preprocessing operations
 *
 * @pre     Called at system initialization stage
 * @post    Default C implementation registered and ready to use
 * @thread_safety No
 */
void img_proc_c_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __IMG_PROC_H__ */