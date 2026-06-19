/* SPDX-License-Identifier: MIT */
/**
 * @file    img_rga.cpp
 * @brief   RK3562 RGA Hardware Image Processing Plugin Implementation
 * @details Wraps RK3562 RGA and MPP SDK into the img_proc interface.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 */

#include "img_rga.h"
#include "img_proc_base.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

/* RK3562 SDK headers */
#include "im2d.h"
#include "rga.h"
#include "RockchipRga.h"

/* TODO: MPP headers (need to verify path) */
// #include "rockchip/rk_mpi.h"

#define MODULE_TAG "[IMG_RGA]"

/* Private context for RGA backend */
typedef struct {
    int width;
    int height;
    int fps;
    void *rga_ctx;      /* TODO: RGA context */
    void *mpp_encoder;  /* TODO: MPP encoder context */
} img_rga_ctx_t;

/* ==========================================================================
 * Lifecycle Management
 * ========================================================================== */

static img_proc_err_t rga_init(img_proc_handle_t *handle)
{
    if (!handle) {
        return IMG_PROC_ERR_PARAM;
    }

    img_rga_ctx_t *ctx = (img_rga_ctx_t *)malloc(sizeof(img_rga_ctx_t));
    if (!ctx) {
        return IMG_PROC_ERR_NO_MEM;
    }

    memset(ctx, 0, sizeof(img_rga_ctx_t));
    ctx->width = handle->config.width;
    ctx->height = handle->config.height;
    ctx->fps = handle->config.fps;

    /* TODO: Initialize RGA context */
    // ctx->rga_ctx = rga_create();
    // if (!ctx->rga_ctx) {
    //     LOG_E(MODULE_TAG "RGA context creation failed");
    //     free(ctx);
    //     return IMG_PROC_ERR_INIT;
    // }

    handle->user_data = ctx;
    LOG_I(MODULE_TAG "RGA hardware image processing initialized");
    return IMG_PROC_OK;
}

static img_proc_err_t rga_deinit(img_proc_handle_t *handle)
{
    if (!handle || !handle->user_data) {
        return IMG_PROC_ERR_PARAM;
    }

    img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;

    /* TODO: Destroy MPP encoder */
    // if (ctx->mpp_encoder) {
    //     rk_mpi_enc_destroy(ctx->mpp_encoder);
    // }

    /* TODO: Destroy RGA context */
    // if (ctx->rga_ctx) {
    //     rga_destroy(ctx->rga_ctx);
    // }

    free(ctx);
    handle->user_data = NULL;
    LOG_I(MODULE_TAG "RGA hardware image processing deinitialized");
    return IMG_PROC_OK;
}

/* ==========================================================================
 * Format Conversion (RGA Hardware Accelerated)
 * ========================================================================== */

static img_proc_err_t rga_yuyv_to_rgb(img_proc_handle_t *handle,
                                       const uint8_t *yuyv, uint8_t *rgb)
{
    /* TODO: Use RGA hardware for YUYV -> RGB conversion */
    // img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
    // rga_format_convert(ctx->rga_ctx, yuyv, ctx->width, ctx->height,
    //                    RGA_FORMAT_YUYV, rgb, ctx->width, ctx->height,
    //                    RGA_FORMAT_RGB888);
    LOG_D(MODULE_TAG "YUYV to RGB conversion (RGA hardware)");
    return IMG_PROC_ERR_UNSUPPORTED;
}

static img_proc_err_t rga_yuyv_to_nv12(img_proc_handle_t *handle,
                                        const uint8_t *yuyv, uint8_t *nv12)
{
    /* TODO: Use RGA hardware for YUYV -> NV12 conversion */
    // img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
    // rga_format_convert(ctx->rga_ctx, yuyv, ctx->width, ctx->height,
    //                    RGA_FORMAT_YUYV, nv12, ctx->width, ctx->height,
    //                    RGA_FORMAT_NV12);
    LOG_D(MODULE_TAG "YUYV to NV12 conversion (RGA hardware)");
    return IMG_PROC_ERR_UNSUPPORTED;
}

static img_proc_err_t rga_yuyv_to_i420(img_proc_handle_t *handle,
                                        const uint8_t *yuyv, uint8_t *i420)
{
    /* TODO: Use RGA hardware for YUYV -> I420 conversion */
    LOG_D(MODULE_TAG "YUYV to I420 conversion (RGA hardware)");
    return IMG_PROC_ERR_UNSUPPORTED;
}

static img_proc_err_t rga_mjpeg_to_rgb(img_proc_handle_t *handle,
                                        const uint8_t *mjpeg, int mjpeg_len,
                                        uint8_t *rgb)
{
    /* TODO: Use hardware MJPEG decoder */
    LOG_D(MODULE_TAG "MJPEG to RGB conversion (RGA hardware)");
    return IMG_PROC_ERR_UNSUPPORTED;
}

/* ==========================================================================
 * Image Resize (RGA Hardware Accelerated)
 * ========================================================================== */

static img_proc_err_t rga_rgb_resize(img_proc_handle_t *handle,
                                      const uint8_t *src_rgb, int src_w, int src_h,
                                      uint8_t *dst_rgb, int dst_w, int dst_h)
{
    /* TODO: Use RGA hardware for resize */
    // img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
    // rga_resize(ctx->rga_ctx, src_rgb, src_w, src_h, dst_rgb, dst_w, dst_h);
    LOG_D(MODULE_TAG "RGB resize (RGA hardware)");
    return IMG_PROC_ERR_UNSUPPORTED;
}

/* ==========================================================================
 * JPEG Compression (Hardware Accelerated)
 * ========================================================================== */

static img_proc_err_t rga_rgb_to_jpeg(img_proc_handle_t *handle,
                                       const uint8_t *rgb, uint8_t *jpeg,
                                       size_t *jpeg_len)
{
    /* TODO: Use RGA hardware for JPEG compression */
    LOG_D(MODULE_TAG "RGB to JPEG compression (RGA hardware)");
    return IMG_PROC_ERR_UNSUPPORTED;
}

/* ==========================================================================
 * H.264 Encoding (MPP Hardware Accelerated)
 * ========================================================================== */

static h264_encoder_t rga_h264_encoder_create(img_proc_handle_t *handle,
                                               const h264_enc_config_t *cfg)
{
    if (!handle || !cfg) {
        return NULL;
    }

    img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;

    /* TODO: Create MPP hardware encoder */
    // ctx->mpp_encoder = rk_mpi_enc_create(cfg->width, cfg->height,
    //                                       cfg->fps, cfg->bitrate,
    //                                       RK_CODEC_ID_H264);
    // if (!ctx->mpp_encoder) {
    //     LOG_E(MODULE_TAG "MPP encoder creation failed");
    //     return NULL;
    // }

    LOG_I(MODULE_TAG "MPP H.264 encoder created (%dx%d, %dfps)",
          cfg->width, cfg->height, cfg->fps);
    return ctx->mpp_encoder;
}

static img_proc_err_t rga_yuyv_to_h264(img_proc_handle_t *handle,
                                        h264_encoder_t encoder,
                                        const uint8_t *yuyv, int yuyv_len,
                                        uint8_t *h264, int *h264_len)
{
    if (!handle || !encoder || !yuyv || !h264 || !h264_len) {
        return IMG_PROC_ERR_PARAM;
    }

    /* TODO: Use MPP hardware encoder */
    // img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
    // int ret = rk_mpi_enc_encode(ctx->mpp_encoder, yuyv, yuyv_len, h264, h264_len);
    // return (ret == 0) ? IMG_PROC_OK : IMG_PROC_ERR_ENCODE;

    LOG_D(MODULE_TAG "YUYV to H.264 encoding (MPP hardware)");
    return IMG_PROC_ERR_UNSUPPORTED;
}

static img_proc_err_t rga_h264_encoder_get_sps_pps(img_proc_handle_t *handle,
                                                    h264_encoder_t encoder,
                                                    uint8_t *sps_pps, int *len)
{
    if (!handle || !encoder || !sps_pps || !len) {
        return IMG_PROC_ERR_PARAM;
    }

    /* TODO: Get SPS/PPS from MPP encoder */
    // img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
    // int ret = rk_mpi_enc_get_sps_pps(ctx->mpp_encoder, sps_pps, len);
    // return (ret == 0) ? IMG_PROC_OK : IMG_PROC_ERR_ENCODE;

    LOG_D(MODULE_TAG "Get SPS/PPS (MPP hardware)");
    return IMG_PROC_ERR_UNSUPPORTED;
}

static void rga_h264_encoder_destroy(img_proc_handle_t *handle,
                                      h264_encoder_t encoder)
{
    /* TODO: Destroy MPP encoder */
    // if (encoder) {
    //     rk_mpi_enc_destroy(encoder);
    // }
    
    if (handle && handle->user_data) {
        img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
        ctx->mpp_encoder = NULL;
    }
}

/* ==========================================================================
 * Drawing Utilities
 * ========================================================================== */

static void rga_bgr_draw_rect(img_proc_handle_t *handle,
                               uint8_t *bgr_data, int width, int height,
                               int x, int y, int w, int h,
                               uint32_t color, int thickness)
{
    /* Drawing is typically done on CPU even with hardware acceleration */
    /* Can use software implementation as fallback */
    LOG_D(MODULE_TAG "BGR draw rect");
}

/* ==========================================================================
 * Operation Table Definition
 * ========================================================================== */

extern "C" {
extern const img_proc_ops_t img_rga_ops = {
    /* Lifecycle */
    .init = rga_init,
    .deinit = rga_deinit,
    
    /* Format conversion */
    .yuyv_to_rgb = rga_yuyv_to_rgb,
    .yuyv_to_nv12 = rga_yuyv_to_nv12,
    .yuyv_to_i420 = rga_yuyv_to_i420,
    .mjpeg_to_rgb = rga_mjpeg_to_rgb,
    
    /* Resize */
    .rgb_resize = rga_rgb_resize,
    
    /* JPEG compression */
    .rgb_to_jpeg = rga_rgb_to_jpeg,
    
    /* H.264 encoding */
    .h264_encoder_create = rga_h264_encoder_create,
    .yuyv_to_h264 = rga_yuyv_to_h264,
    .h264_encoder_get_sps_pps = rga_h264_encoder_get_sps_pps,
    .h264_encoder_destroy = rga_h264_encoder_destroy,
    
    /* Drawing */
    .bgr_draw_rect = rga_bgr_draw_rect
};
}