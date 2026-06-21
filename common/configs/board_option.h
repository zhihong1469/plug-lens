/* SPDX-License-Identifier: MIT */
/**
 * @file    board_option.h
 * @brief   Board Platform Configuration Macros
 * @details Centralized platform selection configuration for cross-platform support.
 *          Defines platform-specific options for i.MX6ULL and RK3562.
 *          Include this file in application code for platform-dependent logic.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-18
 * @version v1.0.0
 * @license MIT License
 */

#ifndef __BOARD_OPTION_H__
#define __BOARD_OPTION_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Platform Selection Macros
 * ========================================================================== */

/**
 * @brief   Platform selection: set to 1 to enable corresponding platform
 * @note    Only ONE platform should be enabled at a time
 * @warning Changing this requires recompilation
 */
#ifndef PLATFORM_IMX6ULL
#define PLATFORM_IMX6ULL  0  /* NXP i.MX6ULL - Default platform */
#endif

#ifndef PLATFORM_RK3562
#define PLATFORM_RK3562   1  /* Rockchip RK3562 - Experimental */
#endif

/* Validate platform selection */
#if (PLATFORM_IMX6ULL && PLATFORM_RK3562)
#error "Only ONE platform can be enabled at a time!"
#endif

#if (!PLATFORM_IMX6ULL && !PLATFORM_RK3562)
#error "At least ONE platform must be enabled!"
#endif

/* ==========================================================================
 * AI Inference Engine Selection (Auto-selected based on platform)
 * ========================================================================== */
/**
 * @brief RK3562 使用 RKNN NPU 硬件加速
 * @note  RKNN 已完成实现，支持 INT8 量化模型
 */
#if PLATFORM_RK3562
    #define AI_ENGINE_RKNN   0  /* RK3562 NPU hardware acceleration - Enabled */
    #define AI_ENGINE_MNN    1  /* CPU-based inference */
#else
    #define AI_ENGINE_RKNN   0
    #define AI_ENGINE_MNN    1  /* i.MX6ULL uses MNN */
#endif

/* ==========================================================================
 * Camera Interface Selection (Auto-selected based on platform)
 * ========================================================================== */

#if PLATFORM_RK3562
    #define CAMERA_INTERFACE_CSI   0  /* RK3562 CSI camera */
    #define CAMERA_INTERFACE_USB   1
#else
    #define CAMERA_INTERFACE_CSI   0
    #define CAMERA_INTERFACE_USB   1  /* i.MX6ULL USB camera */
#endif

/* ==========================================================================
 * Video Encoder Selection (Auto-selected based on platform)
 * ========================================================================== */
/**
 * @brief RK3562 使用 MPP 硬件编码（已集成在 img_rga 插件中）
 * @note  MPP H.264 硬件编码已实现，支持 NV12 输入
 */
#if PLATFORM_RK3562
    #define VIDEO_ENCODER_MPP   1  /* RKMPP hardware encoding - Enabled */
    #define VIDEO_ENCODER_SW    0  /* Software encoding - Not needed */
#else
    #define VIDEO_ENCODER_MPP   0
    #define VIDEO_ENCODER_SW    1  /* i.MX6ULL uses openh264/libjpeg-turbo */
#endif

/* ==========================================================================
 * Image Processing Engine Selection (Auto-selected based on platform)
 * ========================================================================== */

#if PLATFORM_RK3562
    #define IMG_PROC_RGA        1  /* RGA hardware acceleration */
    #define IMG_PROC_SOFTWARE   0  /* libyuv/libjpeg-turbo CPU processing */
#else
    #define IMG_PROC_RGA        0
    #define IMG_PROC_SOFTWARE   1  /* i.MX6ULL uses libyuv/libjpeg-turbo */
#endif

/* ==========================================================================
 * Platform-Specific Configuration
 * ========================================================================== */

#if PLATFORM_IMX6ULL
    #define BOARD_NAME             "i.MX6ULL"
    #define BOARD_CPU_CORES        1
    #define BOARD_DEFAULT_CPU_ID   0
    #define BOARD_HAS_NPU          0
#elif PLATFORM_RK3562
    #define BOARD_NAME             "RK3562"
    #define BOARD_CPU_CORES        4
    #define BOARD_DEFAULT_CPU_ID   0
    #define BOARD_HAS_NPU          1
#endif

/* ==========================================================================
 * Hardware Resource Limits
 * ========================================================================== */

#if PLATFORM_IMX6ULL
    #define MAX_AI_FPS             5    /* Limited by single-core CPU */
    #define MAX_VIDEO_FPS          15   /* USB camera max FPS */
    #define MAX_FRAME_WIDTH        640
    #define MAX_FRAME_HEIGHT       360
#elif PLATFORM_RK3562
    #define MAX_AI_FPS             30   /* NPU acceleration */
    #define MAX_VIDEO_FPS          30   /* CSI camera max FPS */
    #define MAX_FRAME_WIDTH        1920
    #define MAX_FRAME_HEIGHT       1080
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_OPTION_H__ */