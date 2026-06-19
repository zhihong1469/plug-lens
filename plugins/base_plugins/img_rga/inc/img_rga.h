/* SPDX-License-Identifier: MIT */
/**
 * @file    img_rga.h
 * @brief   RK3562 RGA Hardware Image Processing Plugin
 * @details Implementation using RK3562 RGA hardware acceleration and MPP encoder.
 *          This is a base_plugin that implements the img_proc_base.h interface.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Requires RK3562 SDK:
 *          - librga.so (RGA hardware acceleration)
 *          - librockchip_mpp.so (MPP video encoding)
 */

#ifndef __IMG_RGA_H__
#define __IMG_RGA_H__

#include "img_proc_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   RGA hardware image processing operation table
 * @details Exposed for factory pattern, extern declaration
 * @note    This is the concrete implementation for RK3562 platform
 */
extern const img_proc_ops_t img_rga_ops;

#ifdef __cplusplus
}
#endif

#endif /* __IMG_RGA_H__ */