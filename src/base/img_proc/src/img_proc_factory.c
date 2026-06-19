/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_factory.c
 * @brief   Image Processing Factory Implementation
 * @details Factory pattern implementation for image processing module.
 *          Selects backend based on platform configuration macros.
 *          Links to plugins in plugins/base_plugins/.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v1.0.0
 * @license MIT License
 */

#include "img_proc_factory.h"
#include "img_proc_base.h"
#include "board_option.h"

/* Include backend operation tables from plugins */
#if IMG_PROC_SOFTWARE
/* Software backend: img_joint plugin */
extern const img_proc_ops_t img_proc_software_ops;
#elif IMG_PROC_RGA
/* Hardware backend: img_rga plugin */
#include "img_rga.h"
extern const img_proc_ops_t img_rga_ops;
#endif

/**
 * @brief   Create image processing instance based on platform configuration
 */
img_proc_handle_t *img_proc_factory_create(const img_proc_config_t *config)
{
    if (!config) {
        return NULL;
    }

#if IMG_PROC_SOFTWARE
    return img_proc_create(config, &img_proc_software_ops);
#elif IMG_PROC_RGA
    return img_proc_create(config, &img_rga_ops);
#else
    return NULL;
#endif
}

/**
 * @brief   Destroy image processing instance
 */
void img_proc_factory_destroy(img_proc_handle_t *handle)
{
    if (handle) {
        img_proc_destroy(handle);
    }
}