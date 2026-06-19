/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_base.c
 * @brief   Image Processing Base Class Implementation
 * @details Universal base class implementation for image processing module.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 */

#include "img_proc_base.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief   Calculate buffer size for given format
 */
size_t img_proc_calc_buffer_size(img_format_t format, int width, int height)
{
    if (width <= 0 || height <= 0 || format >= IMG_FORMAT_MAX) {
        return 0;
    }

    switch (format) {
        case IMG_FORMAT_YUYV:
            return width * height * 2;  /* 2 bytes per pixel */
        case IMG_FORMAT_RGB888:
        case IMG_FORMAT_BGR888:
            return width * height * 3;  /* 3 bytes per pixel */
        case IMG_FORMAT_NV12:
        case IMG_FORMAT_I420:
            return width * height * 3 / 2;  /* YUV420 */
        case IMG_FORMAT_MJPEG:
        case IMG_FORMAT_JPEG:
            return width * height;  /* Estimate, actual size varies */
        default:
            return 0;
    }
}

/**
 * @brief   Create image processing instance
 */
img_proc_handle_t *img_proc_create(const img_proc_config_t *config,
                                    const img_proc_ops_t *ops)
{
    if (!config || !ops) {
        return NULL;
    }

    img_proc_handle_t *handle = (img_proc_handle_t *)malloc(sizeof(img_proc_handle_t));
    if (!handle) {
        return NULL;
    }

    /* Initialize handle */
    memset(handle, 0, sizeof(img_proc_handle_t));
    handle->ops = ops;
    handle->config = *config;  /* Copy configuration */
    handle->user_data = NULL;

    return handle;
}

/**
 * @brief   Destroy image processing instance
 */
void img_proc_destroy(img_proc_handle_t *handle)
{
    if (!handle) {
        return;
    }

    /* Auto-deinit if initialized */
    if (handle->user_data && handle->ops && handle->ops->deinit) {
        handle->ops->deinit(handle);
    }

    free(handle);
}

/**
 * @brief   Initialize image processing module
 */
img_proc_err_t img_proc_init(img_proc_handle_t *handle)
{
    if (!handle || !handle->ops || !handle->ops->init) {
        return IMG_PROC_ERR_PARAM;
    }
    return handle->ops->init(handle);
}

/**
 * @brief   Deinitialize image processing module
 */
img_proc_err_t img_proc_deinit(img_proc_handle_t *handle)
{
    if (!handle || !handle->ops || !handle->ops->deinit) {
        return IMG_PROC_ERR_PARAM;
    }
    return handle->ops->deinit(handle);
}