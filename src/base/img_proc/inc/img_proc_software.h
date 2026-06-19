/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_software.h
 * @brief   Software-based Image Processing Backend
 * @details Implementation using libyuv, libjpeg-turbo, and openh264.
 *          Optimized for i.MX6ULL platform (CPU-based processing).
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 */

#ifndef __IMG_PROC_SOFTWARE_H__
#define __IMG_PROC_SOFTWARE_H__

#include "img_proc_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Software image processing operation table
 * @details Exposed for factory pattern, extern declaration
 */
extern const img_proc_ops_t img_proc_software_ops;

#ifdef __cplusplus
}
#endif

#endif /* __IMG_PROC_SOFTWARE_H__ */