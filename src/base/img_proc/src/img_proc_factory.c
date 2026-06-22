/* SPDX-License-Identifier: MIT */
/**
 * @file    img_proc_factory.c
 * @brief   Image Processing Factory Implementation
 * @details Factory pattern implementation for image processing module.
 *          Selects backend based on platform configuration macros.
 *          Links to plugins in plugins/base_plugins/.
 *
 *          Architecture (方案C - 混合方案):
 *          - img_proc_convert_create: 单例共享，用于转换/缩放
 *          - img_proc_codec_create: 独立创建，用于编解码
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-19
 * @version v2.0.0
 * @license MIT License
 */

#include "img_proc_factory.h"
#include "img_proc_base.h"
#include "board_option.h"
#include <pthread.h>

/* Include backend operation tables from plugins */
#if IMG_PROC_SOFTWARE
/* Software backend: img_joint plugin */
#include "img_proc_software_ops.h"
extern const img_proc_ops_t img_proc_software_ops;
#elif IMG_PROC_RGA
/* Hardware backend: img_rga plugin for conversion/resize */
#include "img_rga.h"
extern const img_proc_ops_t img_rga_ops;
/* Also include software ops for H.264 encoding (openh264) */
#include "img_proc_software_ops.h"
extern const img_proc_ops_t img_proc_software_ops;
#else
#error "No image processing backend selected! Define IMG_PROC_SOFTWARE or IMG_PROC_RGA"
#endif

/* Singleton instance and thread-safety protection (for convert operations) */
static img_proc_handle_t *g_convert_singleton = NULL;
static pthread_mutex_t   g_convert_mutex      = PTHREAD_MUTEX_INITIALIZER;
static bool              g_convert_initialized = false;

/**
 * @brief   Create image processing instance for format conversion and resize
 * @details Returns a singleton instance. Thread-safe lazy initialization.
 */
img_proc_handle_t *img_proc_convert_create(const img_proc_config_t *config)
{
    pthread_mutex_lock(&g_convert_mutex);

    if (!g_convert_initialized) {
        if (config) {
#if IMG_PROC_SOFTWARE
            g_convert_singleton = img_proc_create(config, &img_proc_software_ops, IMG_PROC_TYPE_CONVERT);
#elif IMG_PROC_RGA
            g_convert_singleton = img_proc_create(config, &img_rga_ops, IMG_PROC_TYPE_CONVERT);
#endif
        }
        if (g_convert_singleton) {
            g_convert_singleton->is_singleton = true;
            img_proc_init(g_convert_singleton);
        }
        g_convert_initialized = true;
    }

    pthread_mutex_unlock(&g_convert_mutex);
    return g_convert_singleton;
}

/**
 * @brief   Create image processing instance for encoding (H.264/JPEG)
 * @details Returns an independent encoder instance with its own configuration.
 *          On RK3562 (RGA mode): Uses openh264 software encoder
 *          On i.MX6ULL (Software mode): Uses openh264 software encoder
 */
img_proc_handle_t *img_proc_codec_create(const img_proc_config_t *config)
{
    if (!config) {
        return NULL;
    }

#if IMG_PROC_SOFTWARE
    return img_proc_create(config, &img_proc_software_ops, IMG_PROC_TYPE_CODEC);
#elif IMG_PROC_RGA
    /* RGA mode: Conversion uses RGA hardware, but H.264 encoding uses openh264 */
    return img_proc_create(config, &img_proc_software_ops, IMG_PROC_TYPE_CODEC);
#else
    return NULL;
#endif
}

/**
 * @brief   Get singleton image processing instance (legacy compatibility)
 */
img_proc_handle_t *img_proc_factory_get_singleton(const img_proc_config_t *config)
{
    return img_proc_convert_create(config);
}

/**
 * @brief   Create image processing instance based on platform configuration (legacy)
 */
img_proc_handle_t *img_proc_factory_create(const img_proc_config_t *config)
{
    if (!config) {
        return NULL;
    }

#if IMG_PROC_SOFTWARE
    return img_proc_create(config, &img_proc_software_ops, IMG_PROC_TYPE_ALL);
#elif IMG_PROC_RGA
    return img_proc_create(config, &img_rga_ops, IMG_PROC_TYPE_ALL);
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
