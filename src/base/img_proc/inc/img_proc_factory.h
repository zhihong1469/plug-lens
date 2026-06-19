/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_factory.h
 * @brief   Image Processing Factory Interface
 * @details Factory pattern for creating image processing instances.
 *          Automatically selects software (libyuv/libjpeg-turbo) or 
 *          hardware (RGA/MPP) backend based on platform configuration.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 */

#ifndef __IMG_PROC_FACTORY_H__
#define __IMG_PROC_FACTORY_H__

#include "img_proc_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Create image processing instance based on platform configuration
 * @details Automatically selects the appropriate backend (software/hardware)
 *          based on the BOARD_OPTION_H settings.
 * @param   config  Pointer to image processing configuration structure
 * @return  Valid image processing handle on success, NULL on failure
 *
 * @pre     config must not be NULL and must contain valid parameters
 * @post    Handle is allocated but module is NOT initialized (call init() to start)
 * @note    Factory pattern hides implementation details from caller
 * @warning Caller is responsible for destroying the handle when done
 *
 * @example
 * @code
 * img_proc_config_t config = {
 *     .width = 640,
 *     .height = 480,
 *     .fps = 30,
 *     .bitrate = 500,
 *     .gop = 15,
 *     .jpeg_quality = 50
 * };
 * img_proc_handle_t *handle = img_proc_factory_create(&config);
 * if (handle) {
 *     handle->ops->init(handle);
 *     // ... use image processing ...
 *     handle->ops->deinit(handle);
 *     img_proc_factory_destroy(handle);
 * }
 * @endcode
 */
img_proc_handle_t *img_proc_factory_create(const img_proc_config_t *config);

/**
 * @brief   Destroy image processing instance
 * @param   handle  Image processing handle to destroy
 * @note    Safe to call with NULL handle
 */
void img_proc_factory_destroy(img_proc_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* __IMG_PROC_FACTORY_H__ */