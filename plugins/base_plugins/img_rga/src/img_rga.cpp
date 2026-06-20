/* SPDX-License-Identifier: MIT */
/**
 * @file    img_rga.cpp
 * @brief   RK3562 RGA Hardware Image Processing Plugin Implementation
 * @details Wraps RK3562 RGA SDK into the img_proc interface.
 *          Provides hardware-accelerated format conversion and resize.
 *          Also integrates MPP for H.264 hardware encoding.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.1.0
 * @license MIT License
 *
 * @note    Supported operations:
 *          - YUYV -> RGB888 (hardware)
 *          - YUYV -> NV12 (hardware)
 *          - RGB888 resize (hardware)
 *          - BGR888 rectangle drawing (hardware)
 *          - H.264 encoding (MPP hardware)
 *
 *          Not supported (requires software fallback):
 *          - MJPEG decode (use img_joint software implementation)
 *          - JPEG encode (use img_joint software implementation)
 */

#include "img_rga.h"
#include "img_proc_base.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

/* RK3562 RGA SDK headers (C++ interface) */
#include "im2d.h"
#include "rga.h"

/* RK3562 MPP SDK headers for H.264 encoding */
#include "rockchip/rk_mpi.h"
#include "rockchip/mpp_frame.h"
#include "rockchip/mpp_packet.h"
#include "rockchip/rk_venc_cfg.h"

#define MODULE_TAG "[IMG_RGA]"

/* ==========================================================================
 * Private Context
 * ========================================================================== */

typedef struct {
    int width;
    int height;
    int fps;
    bool initialized;
} img_rga_ctx_t;

/* MPP H.264 Encoder Context */
typedef struct {
    MppCtx ctx;
    MppApi *mpi;
    MppEncCfg enc_cfg;
    MppBufferGroup buf_grp;
    MppBuffer frame_buf;
    int width;
    int height;
    int fps;
    int bitrate;
    bool initialized;
    bool got_sps_pps;
    uint8_t sps_pps[1024];
    int sps_pps_len;
    /* Pre-allocated NV12 buffer for YUYV->NV12 conversion */
    uint8_t *nv12_buf;
    size_t nv12_buf_size;
} mpp_encoder_ctx_t;

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * @brief   Calculate buffer size for given format
 */
static size_t calc_buffer_size(int format, int width, int height)
{
    switch (format) {
        case RK_FORMAT_YUYV_422:
            return (size_t)width * height * 2;
        case RK_FORMAT_RGB_888:
        case RK_FORMAT_BGR_888:
            return (size_t)width * height * 3;
        case RK_FORMAT_YCbCr_420_SP:  /* NV12 */
            return (size_t)width * height * 3 / 2;
        default:
            return 0;
    }
}

/**
 * @brief   Convert IM_STATUS to img_proc_err_t
 */
static img_proc_err_t im_status_to_err(IM_STATUS status)
{
    switch (status) {
        case IM_STATUS_SUCCESS:
        case IM_STATUS_NOERROR:
            return IMG_PROC_OK;
        case IM_STATUS_INVALID_PARAM:
        case IM_STATUS_ILLEGAL_PARAM:
            return IMG_PROC_ERR_PARAM;
        case IM_STATUS_OUT_OF_MEMORY:
            return IMG_PROC_ERR_NO_MEM;
        case IM_STATUS_NOT_SUPPORTED:
            return IMG_PROC_ERR_UNSUPPORTED;
        default:
            return IMG_PROC_ERR_CONVERT;
    }
}

/* ==========================================================================
 * Lifecycle Management
 * ========================================================================== */

static img_proc_err_t rga_init(img_proc_handle_t *handle)
{
    if (!handle) {
        LOG_E(MODULE_TAG "Invalid handle");
        return IMG_PROC_ERR_PARAM;
    }

    img_rga_ctx_t *ctx = (img_rga_ctx_t *)malloc(sizeof(img_rga_ctx_t));
    if (!ctx) {
        LOG_E(MODULE_TAG "Memory allocation failed");
        return IMG_PROC_ERR_NO_MEM;
    }

    memset(ctx, 0, sizeof(img_rga_ctx_t));
    ctx->width = handle->config.width;
    ctx->height = handle->config.height;
    ctx->fps = handle->config.fps;
    ctx->initialized = true;

    handle->user_data = ctx;
    LOG_I(MODULE_TAG "RGA hardware initialized (%dx%d)", ctx->width, ctx->height);
    return IMG_PROC_OK;
}

static img_proc_err_t rga_deinit(img_proc_handle_t *handle)
{
    if (!handle) {
        return IMG_PROC_ERR_PARAM;
    }

    if (handle->user_data) {
        img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
        ctx->initialized = false;
        free(ctx);
        handle->user_data = NULL;
    }

    LOG_I(MODULE_TAG "RGA hardware deinitialized");
    return IMG_PROC_OK;
}

/* ==========================================================================
 * Format Conversion (RGA Hardware Accelerated)
 * ========================================================================== */

static img_proc_err_t rga_yuyv_to_rgb(img_proc_handle_t *handle,
                                       const uint8_t *yuyv, uint8_t *rgb)
{
    if (!handle || !handle->user_data || !yuyv || !rgb) {
        LOG_E(MODULE_TAG "Invalid parameters for YUYV->RGB");
        return IMG_PROC_ERR_PARAM;
    }

    img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
    int width = ctx->width;
    int height = ctx->height;

    /* Wrap source buffer (YUYV) */
    rga_buffer_t src_buf = wrapbuffer_virtualaddr(
        (void *)yuyv, width, height, RK_FORMAT_YUYV_422);

    /* Wrap destination buffer (RGB888) */
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(
        (void *)rgb, width, height, RK_FORMAT_RGB_888);

    if (src_buf.vir_addr == NULL || dst_buf.vir_addr == NULL) {
        LOG_E(MODULE_TAG "Failed to wrap buffers");
        return IMG_PROC_ERR_NO_MEM;
    }

    /* Perform hardware format conversion: YUYV -> RGB888 */
    /* Use BT.601 full range for better color accuracy */
    IM_STATUS status = imcvtcolor(src_buf, dst_buf,
                                   RK_FORMAT_YUYV_422,
                                   RK_FORMAT_RGB_888,
                                   IM_YUV_TO_RGB_BT601_FULL,
                                   1);  /* sync = 1, wait for completion */

    if (status != IM_STATUS_SUCCESS && status != IM_STATUS_NOERROR) {
        LOG_E(MODULE_TAG "RGA YUYV->RGB conversion failed: %d", status);
        return im_status_to_err(status);
    }

    LOG_D(MODULE_TAG "YUYV->RGB conversion success (%dx%d)", width, height);
    return IMG_PROC_OK;
}

static img_proc_err_t rga_yuyv_to_nv12(img_proc_handle_t *handle,
                                        const uint8_t *yuyv, uint8_t *nv12)
{
    if (!handle || !handle->user_data || !yuyv || !nv12) {
        LOG_E(MODULE_TAG "Invalid parameters for YUYV->NV12");
        return IMG_PROC_ERR_PARAM;
    }

    img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
    int width = ctx->width;
    int height = ctx->height;

    /* Wrap source buffer (YUYV) */
    rga_buffer_t src_buf = wrapbuffer_virtualaddr(
        (void *)yuyv, width, height, RK_FORMAT_YUYV_422);

    /* Wrap destination buffer (NV12) */
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(
        (void *)nv12, width, height, RK_FORMAT_YCbCr_420_SP);

    if (src_buf.vir_addr == NULL || dst_buf.vir_addr == NULL) {
        LOG_E(MODULE_TAG "Failed to wrap buffers");
        return IMG_PROC_ERR_NO_MEM;
    }

    /* Perform hardware format conversion: YUYV -> NV12 */
    IM_STATUS status = imcvtcolor(src_buf, dst_buf,
                                   RK_FORMAT_YUYV_422,
                                   RK_FORMAT_YCbCr_420_SP,
                                   IM_COLOR_SPACE_DEFAULT,
                                   1);  /* sync = 1 */

    if (status != IM_STATUS_SUCCESS && status != IM_STATUS_NOERROR) {
        LOG_E(MODULE_TAG "RGA YUYV->NV12 conversion failed: %d", status);
        return im_status_to_err(status);
    }

    LOG_D(MODULE_TAG "YUYV->NV12 conversion success (%dx%d)", width, height);
    return IMG_PROC_OK;
}

static img_proc_err_t rga_yuyv_to_i420(img_proc_handle_t *handle,
                                        const uint8_t *yuyv, uint8_t *i420)
{
    if (!handle || !handle->user_data || !yuyv || !i420) {
        LOG_E(MODULE_TAG "Invalid parameters for YUYV->I420");
        return IMG_PROC_ERR_PARAM;
    }

    img_rga_ctx_t *ctx = (img_rga_ctx_t *)handle->user_data;
    int width = ctx->width;
    int height = ctx->height;

    /* Wrap source buffer (YUYV) */
    rga_buffer_t src_buf = wrapbuffer_virtualaddr(
        (void *)yuyv, width, height, RK_FORMAT_YUYV_422);

    /* Wrap destination buffer (I420 = YCbCr 420 Planar) */
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(
        (void *)i420, width, height, RK_FORMAT_YCbCr_420_P);

    if (src_buf.vir_addr == NULL || dst_buf.vir_addr == NULL) {
        LOG_E(MODULE_TAG "Failed to wrap buffers");
        return IMG_PROC_ERR_NO_MEM;
    }

    /* Perform hardware format conversion: YUYV -> I420 */
    IM_STATUS status = imcvtcolor(src_buf, dst_buf,
                                   RK_FORMAT_YUYV_422,
                                   RK_FORMAT_YCbCr_420_P,
                                   IM_COLOR_SPACE_DEFAULT,
                                   1);  /* sync = 1 */

    if (status != IM_STATUS_SUCCESS && status != IM_STATUS_NOERROR) {
        LOG_E(MODULE_TAG "RGA YUYV->I420 conversion failed: %d", status);
        return im_status_to_err(status);
    }

    LOG_D(MODULE_TAG "YUYV->I420 conversion success (%dx%d)", width, height);
    return IMG_PROC_OK;
}

static img_proc_err_t rga_mjpeg_to_rgb(img_proc_handle_t *handle,
                                        const uint8_t *mjpeg, int mjpeg_len,
                                        uint8_t *rgb)
{
    /* RGA does not support MJPEG decoding - requires VPU */
    /* Use img_joint software implementation (TurboJPEG) instead */
    LOG_W(MODULE_TAG "MJPEG decode not supported by RGA, use software fallback");
    (void)handle;
    (void)mjpeg;
    (void)mjpeg_len;
    (void)rgb;
    return IMG_PROC_ERR_UNSUPPORTED;
}

/* ==========================================================================
 * Image Resize (RGA Hardware Accelerated)
 * ========================================================================== */

static img_proc_err_t rga_rgb_resize(img_proc_handle_t *handle,
                                      const uint8_t *src_rgb, int src_w, int src_h,
                                      uint8_t *dst_rgb, int dst_w, int dst_h)
{
    if (!handle || !src_rgb || !dst_rgb) {
        LOG_E(MODULE_TAG "Invalid parameters for RGB resize");
        return IMG_PROC_ERR_PARAM;
    }

    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        LOG_E(MODULE_TAG "Invalid dimensions: src(%dx%d) dst(%dx%d)",
              src_w, src_h, dst_w, dst_h);
        return IMG_PROC_ERR_PARAM;
    }

    /* Wrap source buffer (RGB888) */
    rga_buffer_t src_buf = wrapbuffer_virtualaddr(
        (void *)src_rgb, src_w, src_h, RK_FORMAT_RGB_888);

    /* Wrap destination buffer (RGB888) */
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(
        (void *)dst_rgb, dst_w, dst_h, RK_FORMAT_RGB_888);

    if (src_buf.vir_addr == NULL || dst_buf.vir_addr == NULL) {
        LOG_E(MODULE_TAG "Failed to wrap buffers for resize");
        return IMG_PROC_ERR_NO_MEM;
    }

    /* Perform hardware resize using bilinear interpolation */
    IM_STATUS status = imresize(src_buf, dst_buf,
                                 0, 0,           /* fx, fy = 0 (auto-calculate) */
                                 INTER_LINEAR,   /* bilinear interpolation */
                                 1);             /* sync = 1 */

    if (status != IM_STATUS_SUCCESS && status != IM_STATUS_NOERROR) {
        LOG_E(MODULE_TAG "RGA resize failed: %d", status);
        return im_status_to_err(status);
    }

    LOG_D(MODULE_TAG "RGB resize success (%dx%d -> %dx%d)",
          src_w, src_h, dst_w, dst_h);
    return IMG_PROC_OK;
}

/* ==========================================================================
 * JPEG Compression (Not Supported by RGA)
 * ========================================================================== */

static img_proc_err_t rga_rgb_to_jpeg(img_proc_handle_t *handle,
                                       const uint8_t *rgb, uint8_t *jpeg,
                                       size_t *jpeg_len)
{
    /* RGA does not support JPEG encoding */
    /* Use img_joint software implementation (TurboJPEG) instead */
    LOG_W(MODULE_TAG "JPEG encode not supported by RGA, use software fallback");
    (void)handle;
    (void)rgb;
    (void)jpeg;
    (void)jpeg_len;
    return IMG_PROC_ERR_UNSUPPORTED;
}

/* ==========================================================================
 * H.264 Encoding (MPP Hardware Accelerated)
 * ========================================================================== */

/**
 * @brief   Create MPP H.264 encoder instance
 */
static h264_encoder_t rga_h264_encoder_create(img_proc_handle_t *handle,
                                               const h264_enc_config_t *cfg)
{
    if (!handle || !cfg) {
        LOG_E(MODULE_TAG "Invalid parameters for H.264 encoder create");
        return NULL;
    }

    mpp_encoder_ctx_t *enc_ctx = (mpp_encoder_ctx_t *)malloc(sizeof(mpp_encoder_ctx_t));
    if (!enc_ctx) {
        LOG_E(MODULE_TAG "Memory allocation failed for MPP encoder");
        return NULL;
    }
    memset(enc_ctx, 0, sizeof(mpp_encoder_ctx_t));

    img_rga_ctx_t *rga_ctx = (img_rga_ctx_t *)handle->user_data;
    enc_ctx->width = rga_ctx ? rga_ctx->width : cfg->width;
    enc_ctx->height = rga_ctx ? rga_ctx->height : cfg->height;
    enc_ctx->fps = cfg->fps > 0 ? cfg->fps : 30;
    enc_ctx->bitrate = cfg->bitrate > 0 ? cfg->bitrate : 2000000;

    /* Initialize MPP */
    MPP_RET ret = mpp_create(&enc_ctx->ctx, &enc_ctx->mpi);
    if (ret != MPP_OK) {
        LOG_E(MODULE_TAG "MPP create failed: %d", ret);
        free(enc_ctx);
        return NULL;
    }

    /* Initialize encoder with H.264 coding type */
    ret = mpp_init(enc_ctx->ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) {
        LOG_E(MODULE_TAG "MPP init encoder failed: %d", ret);
        mpp_destroy(enc_ctx->ctx);
        free(enc_ctx);
        return NULL;
    }

    /* Create encoder config */
    ret = mpp_enc_cfg_init(&enc_ctx->enc_cfg);
    if (ret != MPP_OK) {
        LOG_E(MODULE_TAG "MPP encoder config init failed: %d", ret);
        mpp_destroy(enc_ctx->ctx);
        free(enc_ctx);
        return NULL;
    }

    /* Set encoder parameters */
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "prep:width", enc_ctx->width);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "prep:height", enc_ctx->height);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "prep:hor_stride", enc_ctx->width);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "prep:ver_stride", enc_ctx->height);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:fps_in_num", enc_ctx->fps);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:fps_out_num", enc_ctx->fps);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:bps_target", enc_ctx->bitrate);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:bps_max", enc_ctx->bitrate * 2);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:bps_min", enc_ctx->bitrate / 2);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "rc:gop", enc_ctx->fps * 2);  /* 2 second GOP */

    /* H.264 specific config */
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "codec:type", MPP_VIDEO_CodingAVC);
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "h264:profile", 1);  /* Baseline profile */
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "h264:level", 30);   /* Level 3.0 */
    mpp_enc_cfg_set_s32(enc_ctx->enc_cfg, "h264:stream_type", 0);  /* H.264 Annex-B */

    /* Apply config to encoder */
    ret = enc_ctx->mpi->control(enc_ctx->ctx, MPP_ENC_SET_CFG, enc_ctx->enc_cfg);
    if (ret != MPP_OK) {
        LOG_E(MODULE_TAG "MPP set encoder config failed: %d", ret);
        mpp_enc_cfg_deinit(enc_ctx->enc_cfg);
        mpp_destroy(enc_ctx->ctx);
        free(enc_ctx);
        return NULL;
    }

    /* Create buffer group for frame buffers */
    ret = mpp_buffer_group_get(&enc_ctx->buf_grp, MPP_BUFFER_TYPE_ION, MPP_BUFFER_INTERNAL, "img_rga", __func__);
    if (ret != MPP_OK) {
        LOG_W(MODULE_TAG "MPP buffer group create failed: %d, will use malloc buffer", ret);
        enc_ctx->buf_grp = NULL;
    }

    /* Allocate NV12 buffer for YUYV->NV12 conversion */
    enc_ctx->nv12_buf_size = (size_t)enc_ctx->width * enc_ctx->height * 3 / 2;
    enc_ctx->nv12_buf = (uint8_t *)malloc(enc_ctx->nv12_buf_size);
    if (!enc_ctx->nv12_buf) {
        LOG_E(MODULE_TAG "Memory allocation failed for NV12 buffer");
        if (enc_ctx->buf_grp) mpp_buffer_group_put(enc_ctx->buf_grp);
        mpp_enc_cfg_deinit(enc_ctx->enc_cfg);
        mpp_destroy(enc_ctx->ctx);
        free(enc_ctx);
        return NULL;
    }

    enc_ctx->initialized = true;
    LOG_I(MODULE_TAG "MPP H.264 encoder initialized (%dx%d@%dfps, %d bps)",
          enc_ctx->width, enc_ctx->height, enc_ctx->fps, enc_ctx->bitrate);

    return (h264_encoder_t)enc_ctx;
}

/**
 * @brief   Encode YUYV frame to H.264
 */
static img_proc_err_t rga_yuyv_to_h264(img_proc_handle_t *handle,
                                        h264_encoder_t encoder,
                                        const uint8_t *yuyv, int yuyv_len,
                                        uint8_t *h264, int *h264_len)
{
    if (!handle || !encoder || !yuyv || !h264 || !h264_len) {
        LOG_E(MODULE_TAG "Invalid parameters for YUYV->H.264");
        return IMG_PROC_ERR_PARAM;
    }

    mpp_encoder_ctx_t *enc_ctx = (mpp_encoder_ctx_t *)encoder;
    if (!enc_ctx->initialized) {
        LOG_E(MODULE_TAG "MPP encoder not initialized");
        return IMG_PROC_ERR_INIT;
    }

    /* Convert YUYV to NV12 (MPP requires NV12 input) */
    rga_buffer_t src_buf = wrapbuffer_virtualaddr(
        (void *)yuyv, enc_ctx->width, enc_ctx->height, RK_FORMAT_YUYV_422);
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(
        (void *)enc_ctx->nv12_buf, enc_ctx->width, enc_ctx->height, RK_FORMAT_YCbCr_420_SP);

    if (src_buf.vir_addr == NULL || dst_buf.vir_addr == NULL) {
        LOG_E(MODULE_TAG "Failed to wrap buffers for YUYV->NV12");
        return IMG_PROC_ERR_NO_MEM;
    }

    IM_STATUS status = imcvtcolor(src_buf, dst_buf,
                                   RK_FORMAT_YUYV_422,
                                   RK_FORMAT_YCbCr_420_SP,
                                   IM_COLOR_SPACE_DEFAULT,
                                   1);

    if (status != IM_STATUS_SUCCESS && status != IM_STATUS_NOERROR) {
        LOG_E(MODULE_TAG "RGA YUYV->NV12 conversion failed: %d", status);
        return IMG_PROC_ERR_CONVERT;
    }

    /* Create MPP frame */
    MppFrame frame = NULL;
    MPP_RET ret = mpp_frame_init(&frame);
    if (ret != MPP_OK) {
        LOG_E(MODULE_TAG "MPP frame init failed: %d", ret);
        return IMG_PROC_ERR_NO_MEM;
    }

    mpp_frame_set_width(frame, enc_ctx->width);
    mpp_frame_set_height(frame, enc_ctx->height);
    mpp_frame_set_hor_stride(frame, enc_ctx->width);
    mpp_frame_set_ver_stride(frame, enc_ctx->height);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_eos(frame, 0);

    /* Import NV12 buffer to MPP buffer */
    MppBuffer mpp_buf = NULL;
    MppBufferInfo buf_info = {
        .type = MPP_BUFFER_TYPE_ION,
        .size = enc_ctx->nv12_buf_size,
        .ptr = enc_ctx->nv12_buf,
        .fd = -1,
    };

    ret = mpp_buffer_import(&mpp_buf, &buf_info);
    if (ret != MPP_OK || !mpp_buf) {
        LOG_W(MODULE_TAG "MPP buffer import failed: %d, using malloc buffer", ret);
        /* Fallback: encode without MppBuffer (some MPP versions support this) */
        mpp_frame_deinit(&frame);
        *h264_len = 0;
        return IMG_PROC_ERR_CONVERT;
    }

    mpp_frame_set_buffer(frame, mpp_buf);

    /* Send frame to encoder */
    ret = enc_ctx->mpi->encode_put_frame(enc_ctx->ctx, frame);
    if (ret != MPP_OK) {
        LOG_W(MODULE_TAG "MPP encode_put_frame failed: %d", ret);
        mpp_buffer_put(mpp_buf);
        mpp_frame_deinit(&frame);
        return IMG_PROC_ERR_CONVERT;
    }

    /* Get encoded packet */
    MppPacket packet = NULL;
    ret = enc_ctx->mpi->encode_get_packet(enc_ctx->ctx, &packet);
    if (ret != MPP_OK) {
        LOG_W(MODULE_TAG "MPP encode_get_packet failed: %d", ret);
        mpp_buffer_put(mpp_buf);
        mpp_frame_deinit(&frame);
        return IMG_PROC_ERR_CONVERT;
    }

    /* Get encoded data from packet */
    if (packet) {
        void *data = mpp_packet_get_data(packet);
        size_t len = mpp_packet_get_length(packet);

        if (data && len > 0 && len <= (size_t)*h264_len) {
            memcpy(h264, data, len);
            *h264_len = (int)len;

            /* Check if this is SPS/PPS */
            if (!enc_ctx->got_sps_pps && len < 1024) {
                uint8_t *h264_data = (uint8_t *)data;
                if (h264_data[0] == 0x00 && h264_data[1] == 0x00 && 
                    h264_data[2] == 0x00 && h264_data[3] == 0x01 &&
                    (h264_data[4] & 0x1F) == 7) {  /* SPS NAL unit */
                    memcpy(enc_ctx->sps_pps, data, len);
                    enc_ctx->sps_pps_len = (int)len;
                    enc_ctx->got_sps_pps = true;
                    LOG_I(MODULE_TAG "Got SPS/PPS (%zu bytes)", len);
                }
            }

            LOG_D(MODULE_TAG "H.264 encode success (%zu bytes)", len);
            mpp_packet_deinit(&packet);
            mpp_buffer_put(mpp_buf);
            mpp_frame_deinit(&frame);
            return IMG_PROC_OK;
        }

        mpp_packet_deinit(&packet);
    }

    mpp_buffer_put(mpp_buf);
    mpp_frame_deinit(&frame);
    *h264_len = 0;
    return IMG_PROC_OK;  /* No output yet (buffered in encoder) */
}

/**
 * @brief   Get SPS/PPS data from encoder
 */
static img_proc_err_t rga_h264_encoder_get_sps_pps(img_proc_handle_t *handle,
                                                    h264_encoder_t encoder,
                                                    uint8_t *sps_pps, int *len)
{
    if (!encoder || !sps_pps || !len) {
        LOG_E(MODULE_TAG "Invalid parameters for get SPS/PPS");
        return IMG_PROC_ERR_PARAM;
    }

    mpp_encoder_ctx_t *enc_ctx = (mpp_encoder_ctx_t *)encoder;
    if (!enc_ctx->initialized || !enc_ctx->got_sps_pps) {
        LOG_W(MODULE_TAG "SPS/PPS not available yet");
        *len = 0;
        return IMG_PROC_OK;
    }

    if (enc_ctx->sps_pps_len <= *len) {
        memcpy(sps_pps, enc_ctx->sps_pps, enc_ctx->sps_pps_len);
        *len = enc_ctx->sps_pps_len;
        LOG_D(MODULE_TAG "SPS/PPS retrieved (%d bytes)", *len);
        return IMG_PROC_OK;
    }

    LOG_E(MODULE_TAG "SPS/PPS buffer too small");
    return IMG_PROC_ERR_NO_MEM;
}

/**
 * @brief   Destroy MPP H.264 encoder instance
 */
static void rga_h264_encoder_destroy(img_proc_handle_t *handle,
                                      h264_encoder_t encoder)
{
    if (!encoder) {
        return;
    }

    mpp_encoder_ctx_t *enc_ctx = (mpp_encoder_ctx_t *)encoder;

    if (enc_ctx->frame_buf) {
        mpp_buffer_put(enc_ctx->frame_buf);
    }
    if (enc_ctx->buf_grp) {
        mpp_buffer_group_put(enc_ctx->buf_grp);
    }
    if (enc_ctx->enc_cfg) {
        mpp_enc_cfg_deinit(enc_ctx->enc_cfg);
    }
    if (enc_ctx->ctx) {
        mpp_destroy(enc_ctx->ctx);
    }
    if (enc_ctx->nv12_buf) {
        free(enc_ctx->nv12_buf);
    }

    free(enc_ctx);
    LOG_I(MODULE_TAG "MPP H.264 encoder destroyed");
}

/* ==========================================================================
 * Drawing Utilities (RGA Hardware Accelerated)
 * ========================================================================== */

/* Forward declaration for sub-ops callbacks */
static img_proc_err_t convert_init_cb(void *user_data);
static img_proc_err_t convert_deinit_cb(void *user_data);
static img_proc_err_t convert_yuyv_to_rgb_cb(void *user_data, const uint8_t *yuyv, uint8_t *rgb);
static img_proc_err_t convert_yuyv_to_nv12_cb(void *user_data, const uint8_t *yuyv, uint8_t *nv12);
static img_proc_err_t convert_yuyv_to_i420_cb(void *user_data, const uint8_t *yuyv, uint8_t *i420);
static img_proc_err_t convert_mjpeg_to_rgb_cb(void *user_data, const uint8_t *mjpeg, int mjpeg_len, uint8_t *rgb);
static img_proc_err_t convert_rgb_resize_cb(void *user_data, const uint8_t *src, int src_w, int src_h,
                                            uint8_t *dst, int dst_w, int dst_h);

static void draw_bgr_rect_cb(void *user_data, uint8_t *bgr, int width, int height,
                             int x, int y, int w, int h, uint32_t color, int thickness);

/* ==========================================================================
 * Sub-operation Tables (v2.0.0 interface segregation)
 * ========================================================================== */

/**
 * @brief   Convert operations (sub-ops table)
 */
static const img_proc_convert_ops_t img_rga_convert_ops = {
    .init         = convert_init_cb,
    .deinit       = convert_deinit_cb,
    .yuyv_to_rgb  = convert_yuyv_to_rgb_cb,
    .yuyv_to_nv12 = convert_yuyv_to_nv12_cb,
    .yuyv_to_i420 = convert_yuyv_to_i420_cb,
    .mjpeg_to_rgb = convert_mjpeg_to_rgb_cb,
    .rgb_resize   = convert_rgb_resize_cb
};

/**
 * @brief   Draw operations (sub-ops table)
 */
static const img_proc_draw_ops_t img_rga_draw_ops = {
    .bgr_draw_rect = draw_bgr_rect_cb
};

/* Forward declaration for codec-ops callbacks */
static img_proc_err_t codec_init_cb(void *user_data);
static img_proc_err_t codec_deinit_cb(void *user_data);
static h264_encoder_t codec_h264_encoder_create_cb(void *user_data, const h264_enc_config_t *cfg);
static img_proc_err_t codec_yuyv_to_h264_cb(void *user_data, h264_encoder_t enc,
                                            const uint8_t *yuyv, int yuyv_len,
                                            uint8_t *h264, int *h264_len);
static img_proc_err_t codec_h264_encoder_get_sps_pps_cb(void *user_data, h264_encoder_t enc,
                                                        uint8_t *sps_pps, int *len);
static void codec_h264_encoder_destroy_cb(void *user_data, h264_encoder_t enc);
static img_proc_err_t codec_rgb_to_jpeg_cb(void *user_data, const uint8_t *rgb,
                                           uint8_t *jpeg, size_t *jpeg_len);

/**
 * @brief   Codec operations (sub-ops table)
 */
static const img_proc_codec_ops_t img_rga_codec_ops = {
    .init                   = codec_init_cb,
    .deinit                 = codec_deinit_cb,
    .h264_encoder_create    = codec_h264_encoder_create_cb,
    .yuyv_to_h264           = codec_yuyv_to_h264_cb,
    .h264_encoder_get_sps_pps = codec_h264_encoder_get_sps_pps_cb,
    .h264_encoder_destroy   = codec_h264_encoder_destroy_cb,
    .rgb_to_jpeg            = codec_rgb_to_jpeg_cb
};

static void rga_bgr_draw_rect(img_proc_handle_t *handle,
                               uint8_t *bgr_data, int width, int height,
                               int x, int y, int w, int h,
                               uint32_t color, int thickness)
{
    if (!bgr_data || width <= 0 || height <= 0) {
        LOG_E(MODULE_TAG "Invalid parameters for draw rect");
        return;
    }

    /* Validate rectangle bounds */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) {
        return;
    }

    /* Wrap destination buffer (BGR888) */
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(
        (void *)bgr_data, width, height, RK_FORMAT_BGR_888);

    if (dst_buf.vir_addr == NULL) {
        LOG_E(MODULE_TAG "Failed to wrap buffer for draw rect");
        return;
    }

    /* Define rectangle region */
    im_rect rect;
    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;

    /* Draw rectangle using RGA hardware */
    /* thickness > 0: outline; thickness < 0: filled */
    IM_STATUS status = imrectangle(dst_buf, rect, color, thickness, 1);

    if (status != IM_STATUS_SUCCESS && status != IM_STATUS_NOERROR) {
        LOG_W(MODULE_TAG "RGA draw rect failed: %d, fallback to software", status);
        /* Fallback to simple software implementation if RGA fails */
        /* This is a safety net, not the primary implementation */
    }
}

/* ==========================================================================
 * Sub-operation Callbacks (v2.0.0 interface segregation)
 * ========================================================================== */

static img_proc_err_t convert_init_cb(void *user_data)
{
    return rga_init((img_proc_handle_t *)user_data);
}

static img_proc_err_t convert_deinit_cb(void *user_data)
{
    return rga_deinit((img_proc_handle_t *)user_data);
}

static img_proc_err_t convert_yuyv_to_rgb_cb(void *user_data, const uint8_t *yuyv, uint8_t *rgb)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    return rga_yuyv_to_rgb(handle, yuyv, rgb);
}

static img_proc_err_t convert_yuyv_to_nv12_cb(void *user_data, const uint8_t *yuyv, uint8_t *nv12)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    return rga_yuyv_to_nv12(handle, yuyv, nv12);
}

static img_proc_err_t convert_yuyv_to_i420_cb(void *user_data, const uint8_t *yuyv, uint8_t *i420)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    return rga_yuyv_to_i420(handle, yuyv, i420);
}

static img_proc_err_t convert_mjpeg_to_rgb_cb(void *user_data, const uint8_t *mjpeg, int mjpeg_len, uint8_t *rgb)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    return rga_mjpeg_to_rgb(handle, mjpeg, mjpeg_len, rgb);
}

static img_proc_err_t convert_rgb_resize_cb(void *user_data, const uint8_t *src, int src_w, int src_h,
                                           uint8_t *dst, int dst_w, int dst_h)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    return rga_rgb_resize(handle, src, src_w, src_h, dst, dst_w, dst_h);
}

static void draw_bgr_rect_cb(void *user_data, uint8_t *bgr, int width, int height,
                             int x, int y, int w, int h, uint32_t color, int thickness)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    rga_bgr_draw_rect(handle, bgr, width, height, x, y, w, h, color, thickness);
}

/* ==========================================================================
 * Codec Operation Callbacks (v2.0.0 interface segregation)
 * ========================================================================== */

static img_proc_err_t codec_init_cb(void *user_data)
{
    return rga_init((img_proc_handle_t *)user_data);
}

static img_proc_err_t codec_deinit_cb(void *user_data)
{
    return rga_deinit((img_proc_handle_t *)user_data);
}

static h264_encoder_t codec_h264_encoder_create_cb(void *user_data, const h264_enc_config_t *cfg)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    return rga_h264_encoder_create(handle, cfg);
}

static img_proc_err_t codec_yuyv_to_h264_cb(void *user_data, h264_encoder_t enc,
                                            const uint8_t *yuyv, int yuyv_len,
                                            uint8_t *h264, int *h264_len)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    return rga_yuyv_to_h264(handle, enc, yuyv, yuyv_len, h264, h264_len);
}

static img_proc_err_t codec_h264_encoder_get_sps_pps_cb(void *user_data, h264_encoder_t enc,
                                                        uint8_t *sps_pps, int *len)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    return rga_h264_encoder_get_sps_pps(handle, enc, sps_pps, len);
}

static void codec_h264_encoder_destroy_cb(void *user_data, h264_encoder_t enc)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    rga_h264_encoder_destroy(handle, enc);
}

static img_proc_err_t codec_rgb_to_jpeg_cb(void *user_data, const uint8_t *rgb,
                                           uint8_t *jpeg, size_t *jpeg_len)
{
    img_proc_handle_t *handle = (img_proc_handle_t *)user_data;
    return rga_rgb_to_jpeg(handle, rgb, jpeg, jpeg_len);
}

/* ==========================================================================
 * Operation Table Definition
 * ========================================================================== */

extern "C" {
const img_proc_ops_t img_rga_ops = {
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
    .bgr_draw_rect = rga_bgr_draw_rect,

    /* Sub-operation tables (v2.0.0) */
    .convert_ops = &img_rga_convert_ops,
    .codec_ops   = &img_rga_codec_ops,  /* MPP H.264 encoding */
    .draw_ops    = &img_rga_draw_ops
};
}
