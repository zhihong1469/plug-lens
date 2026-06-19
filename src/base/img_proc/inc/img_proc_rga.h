/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_rga.h
 * @brief   RGA Hardware-based Image Processing Backend
 * @details Implementation using RK3562 RGA hardware acceleration.
 *          Placeholder for future SDK integration.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 *
 * @note    This is a placeholder file. Full implementation requires:
 *          - RK3562 RGA SDK integration
 *          - MPP encoder integration
 *          - Hardware node verification (/dev/rga, /dev/mpp_service)
 */

#ifndef __IMG_PROC_RGA_H__
#define __IMG_PROC_RGA_H__

#include "img_proc_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   RGA hardware image processing operation table
 * @details Exposed for factory pattern, extern declaration
 * @note    Implementation pending - requires RK3562 SDK integration
 */
extern const img_proc_ops_t img_proc_rga_ops;

#ifdef __cplusplus
}
#endif

#endif /* __IMG_PROC_RGA_H__ */