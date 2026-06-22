/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_factory.h
 * @brief   Image Processing Factory Interface
 * @details Factory pattern for creating image processing instances.
 *          Automatically selects software (libyuv/libjpeg-turbo) or 
 *          hardware (RGA/MPP) backend based on platform configuration.
 *
 *          Architecture (方案C - 混合方案):
 *          - img_proc_convert_create: 获取转换/缩放能力（单例共享）
 *          - img_proc_codec_create: 获取编解码能力（独立创建）
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v2.0.0
 * @license MIT License
 */

#ifndef __IMG_PROC_FACTORY_H__
#define __IMG_PROC_FACTORY_H__

#include "img_proc_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Create image processing instance for format conversion and resize
 * @details Returns a singleton instance for format conversion/resize operations.
 *          The singleton is lazily initialized and shared across all callers.
 * @param   config  Pointer to configuration
 * @return  Shared singleton handle on success, NULL on failure
 * @note    Thread-safe. Caller must NOT call factory_destroy() on returned handle.
 * @see     img_proc_codec_create() for encoder instance
 */
img_proc_handle_t *img_proc_convert_create(const img_proc_config_t *config);

/**
 * @brief   Create image processing instance for encoding (H.264/JPEG)
 * @details Returns an independent instance for encoding operations.
 *          Each call creates a new encoder with its own configuration.
 * @param   config  Pointer to configuration
 * @return  Independent encoder handle on success, NULL on failure
 * @note    Caller MUST call factory_destroy() when done.
 * @see     img_proc_convert_create() for shared conversion instance
 */
img_proc_handle_t *img_proc_codec_create(const img_proc_config_t *config);

/**
 * @brief   Get singleton image processing instance (legacy compatibility)
 * @details Equivalent to img_proc_convert_create(). Kept for backward compatibility.
 * @param   config  Pointer to configuration
 * @return  Shared singleton handle on success, NULL on failure
 * @deprecated Use img_proc_convert_create() instead
 */
img_proc_handle_t *img_proc_factory_get_singleton(const img_proc_config_t *config);

/**
 * @brief   Create image processing instance based on platform configuration (legacy)
 * @details Kept for backward compatibility. Use img_proc_convert_create() or
 *          img_proc_codec_create() for new code.
 * @param   config  Pointer to configuration
 * @return  Valid image processing handle on success, NULL on failure
 * @deprecated Use img_proc_convert_create() or img_proc_codec_create() instead
 */
img_proc_handle_t *img_proc_factory_create(const img_proc_config_t *config);

/**
 * @brief   Destroy image processing instance
 * @param   handle  Image processing handle to destroy
 * @note    Safe to call with NULL handle. Singleton handles are silently ignored.
 */
void img_proc_factory_destroy(img_proc_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* __IMG_PROC_FACTORY_H__ */
