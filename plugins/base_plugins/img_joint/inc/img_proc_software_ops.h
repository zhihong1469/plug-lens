/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_software_ops.h
 * @brief   Software Image Processing Backend Interface
 * @details Export declaration for software backend operation table.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 */

#ifndef __IMG_PROC_SOFTWARE_OPS_H__
#define __IMG_PROC_SOFTWARE_OPS_H__

#include "img_proc_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Software backend operation table
 */
extern const img_proc_ops_t img_proc_software_ops;

#ifdef __cplusplus
}
#endif

#endif /* __IMG_PROC_SOFTWARE_OPS_H__ */
