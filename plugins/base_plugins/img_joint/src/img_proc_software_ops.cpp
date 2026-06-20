/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_software_ops.cpp
 * @brief   Software-based Image Processing Backend Implementation
 * @details Wraps existing img_joint.h functions into factory pattern.
 *          Uses libyuv, libjpeg-turbo, and openh264.
 *          This is a base_plugin that implements the img_proc_base.h interface.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 */

#include "img_proc_base.h"
#include "img_joint.h"
#include <stdlib.h>
#include <string.h>

/* Private context for software backend */
typedef struct {
    int width;
    int height;
    int fps;
    int jpeg_quality;
    h264_encoder_t h264_encoder;
} img_proc_software_ctx_t;

/* ==========================================================================
 * Lifecycle Management
 * ========================================================================== */

static img_proc_err_t software_init(img_proc_handle_t *handle)
{
    if (!handle) {
        return IMG_PROC_ERR_PARAM;
    }

    img_proc_software_ctx_t *ctx = (img_proc_software_ctx_t *)malloc(sizeof(img_proc_software_ctx_t));
    if (!ctx) {
        return IMG_PROC_ERR_NO_MEM;
    }

    memset(ctx, 0, sizeof(img_proc_software_ctx_t));
    ctx->width = handle->config.width;
    ctx->height = handle->config.height;
    ctx->fps = handle->config.fps;
    ctx->jpeg_quality = handle->config.jpeg_quality;

    handle->user_data = ctx;
    return IMG_PROC_OK;
}

static img_proc_err_t software_deinit(img_proc_handle_t *handle)
{
    if (!handle || !handle->user_data) {
        return IMG_PROC_ERR_PARAM;
    }

    img_proc_software_ctx_t *ctx = (img_proc_software_ctx_t *)handle->user_data;
    
    /* Destroy H264 encoder if created */
    if (ctx->h264_encoder) {
        h264_encoder_destroy(ctx->h264_encoder);
        ctx->h264_encoder = NULL;
    }

    free(ctx);
    handle->user_data = NULL;
    return IMG_PROC_OK;
}

/* ==========================================================================
 * Format Conversion
 * ========================================================================== */

static img_proc_err_t software_yuyv_to_rgb(img_proc_handle_t *handle,
                                            const uint8_t *yuyv, uint8_t *rgb)
{
    if (!handle || !yuyv || !rgb) {
        return IMG_PROC_ERR_PARAM;
    }

    img_proc_software_ctx_t *ctx = (img_proc_software_ctx_t *)handle->user_data;
    int ret = yuyv_to_rgb(yuyv, ctx->width, ctx->height, rgb);
    
    return (ret == IMG_JOINT_OK) ? IMG_PROC_OK : IMG_PROC_ERR_CONVERT;
}

static img_proc_err_t software_yuyv_to_nv12(img_proc_handle_t *handle,
                                             const uint8_t *yuyv, uint8_t *nv12)
{
    if (!handle || !yuyv || !nv12) {
        return IMG_PROC_ERR_PARAM;
    }

    /* Note: NV12 is similar to I420 but with different UV layout */
    img_proc_software_ctx_t *ctx = (img_proc_software_ctx_t *)handle->user_data;
    int ret = yuyv_to_i420(yuyv, ctx->width, ctx->height, nv12);
    
    return (ret == IMG_JOINT_OK) ? IMG_PROC_OK : IMG_PROC_ERR_CONVERT;
}

static img_proc_err_t software_yuyv_to_i420(img_proc_handle_t *handle,
                                             const uint8_t *yuyv, uint8_t *i420)
{
    if (!handle || !yuyv || !i420) {
        return IMG_PROC_ERR_PARAM;
    }

    img_proc_software_ctx_t *ctx = (img_proc_software_ctx_t *)handle->user_data;
    int ret = yuyv_to_i420(yuyv, ctx->width, ctx->height, i420);
    
    return (ret == IMG_JOINT_OK) ? IMG_PROC_OK : IMG_PROC_ERR_CONVERT;
}

/**
 * @brief   MJPEG to RGB conversion (software backend)
 * @note    Exported for external fallback use (e.g., RGA doesn't support MJPEG decode)
 */
img_proc_err_t software_mjpeg_to_rgb(img_proc_handle_t *handle,
                                     const uint8_t *mjpeg, int mjpeg_len,
                                     uint8_t *rgb)
{
    if (!handle || !mjpeg || !rgb) {
        return IMG_PROC_ERR_PARAM;
    }

    img_proc_software_ctx_t *ctx = (img_proc_software_ctx_t *)handle->user_data;
    int ret = mjpeg_to_rgb(mjpeg, mjpeg_len, ctx->width, ctx->height, rgb);
    
    return (ret == IMG_JOINT_OK) ? IMG_PROC_OK : IMG_PROC_ERR_CONVERT;
}

/* ==========================================================================
 * Image Resize
 * ========================================================================== */

static img_proc_err_t software_rgb_resize(img_proc_handle_t *handle,
                                           const uint8_t *src_rgb, int src_w, int src_h,
                                           uint8_t *dst_rgb, int dst_w, int dst_h)
{
    if (!handle || !src_rgb || !dst_rgb) {
        return IMG_PROC_ERR_PARAM;
    }

    int ret = rgb_resize(src_rgb, src_w, src_h, dst_rgb, dst_w, dst_h);
    return (ret == IMG_JOINT_OK) ? IMG_PROC_OK : IMG_PROC_ERR_CONVERT;
}

/* ==========================================================================
 * JPEG Compression
 * ========================================================================== */

static img_proc_err_t software_rgb_to_jpeg(img_proc_handle_t *handle,
                                            const uint8_t *rgb, uint8_t *jpeg,
                                            size_t *jpeg_len)
{
    /* Note: This requires integration with img_storage or TurboJPEG directly */
    /* For now, return unsupported - will be implemented later */
    return IMG_PROC_ERR_UNSUPPORTED;
}

/* ==========================================================================
 * H.264 Encoding
 * ========================================================================== */

static h264_encoder_t software_h264_encoder_create(img_proc_handle_t *handle,
                                                    const h264_enc_config_t *cfg)
{
    if (!handle || !cfg) {
        return NULL;
    }

    img_proc_software_ctx_t *ctx = (img_proc_software_ctx_t *)handle->user_data;
    
    h264_encode_param_t param = {
        .width = cfg->width,
        .height = cfg->height,
        .fps = cfg->fps,
        .bitrate = cfg->bitrate,
        .gop = cfg->gop,
        .use_cpu_core = cfg->use_cpu_core
    };

    ctx->h264_encoder = h264_encoder_create(&param);
    return ctx->h264_encoder;
}

static img_proc_err_t software_yuyv_to_h264(img_proc_handle_t *handle,
                                             h264_encoder_t encoder,
                                             const uint8_t *yuyv, int yuyv_len,
                                             uint8_t *h264, int *h264_len)
{
    if (!handle || !encoder || !yuyv || !h264 || !h264_len) {
        return IMG_PROC_ERR_PARAM;
    }

    int ret = yuyv_to_h264(encoder, yuyv, yuyv_len, h264, h264_len);
    return (ret == IMG_JOINT_OK) ? IMG_PROC_OK : IMG_PROC_ERR_ENCODE;
}

static img_proc_err_t software_h264_encoder_get_sps_pps(img_proc_handle_t *handle,
                                                         h264_encoder_t encoder,
                                                         uint8_t *sps_pps, int *len)
{
    if (!handle || !encoder || !sps_pps || !len) {
        return IMG_PROC_ERR_PARAM;
    }

    int ret = h264_encoder_get_sps_pps(encoder, sps_pps, len);
    return (ret == IMG_JOINT_OK) ? IMG_PROC_OK : IMG_PROC_ERR_ENCODE;
}

static void software_h264_encoder_destroy(img_proc_handle_t *handle,
                                           h264_encoder_t encoder)
{
    if (encoder) {
        h264_encoder_destroy(encoder);
    }
    
    if (handle && handle->user_data) {
        img_proc_software_ctx_t *ctx = (img_proc_software_ctx_t *)handle->user_data;
        ctx->h264_encoder = NULL;
    }
}

/* ==========================================================================
 * Drawing Utilities
 * ========================================================================== */

static void software_bgr_draw_rect(img_proc_handle_t *handle,
                                    uint8_t *bgr_data, int width, int height,
                                    int x, int y, int w, int h,
                                    uint32_t color, int thickness)
{
    /* Directly call img_joint function */
    bgr_draw_rect(bgr_data, width, height, x, y, w, h, color, thickness);
}

/* ==========================================================================
 * Operation Table Definition
 * ========================================================================== */

extern "C" {
extern const img_proc_ops_t img_proc_software_ops = {
    /* Lifecycle */
    .init = software_init,
    .deinit = software_deinit,
    
    /* Format conversion */
    .yuyv_to_rgb = software_yuyv_to_rgb,
    .yuyv_to_nv12 = software_yuyv_to_nv12,
    .yuyv_to_i420 = software_yuyv_to_i420,
    .mjpeg_to_rgb = software_mjpeg_to_rgb,
    
    /* Resize */
    .rgb_resize = software_rgb_resize,
    
    /* JPEG compression */
    .rgb_to_jpeg = software_rgb_to_jpeg,
    
    /* H.264 encoding */
    .h264_encoder_create = software_h264_encoder_create,
    .yuyv_to_h264 = software_yuyv_to_h264,
    .h264_encoder_get_sps_pps = software_h264_encoder_get_sps_pps,
    .h264_encoder_destroy = software_h264_encoder_destroy,
    
    /* Drawing */
    .bgr_draw_rect = software_bgr_draw_rect
};
}