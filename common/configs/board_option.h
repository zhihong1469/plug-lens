/* SPDX-License-Identifier: MIT */
/**
 * @file    board_option.h
 * @brief   Board Platform Configuration Macros
 * @details Centralized platform selection configuration for cross-platform support.
 *          Defines platform-specific options for i.MX6ULL and RK3562.
 *          Include this file in application code for platform-dependent logic.
 * 
 * @note    Configuration can be set via:
 *          1. Command-line: make PLATFORM=rk3562 ENGINE=hardware
 *          2. Configuration file: common/configs/build_config.mk
 *          3. Default values in this file
 * 
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-22
 * @version v2.0.0
 * @license MIT License
 */

#ifndef __BOARD_OPTION_H__
#define __BOARD_OPTION_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Platform Selection Macros (Auto-detected from Makefile)
 * ========================================================================== */

/**
 * @brief   Platform selection: set to 1 to enable corresponding platform
 * @note    These are typically defined by Makefile, not hardcoded here
 * @warning Only ONE platform should be enabled at a time
 */
#ifndef PLATFORM_IMX6ULL
#define PLATFORM_IMX6ULL  0  /* NXP i.MX6ULL */
#endif

#ifndef PLATFORM_RK3562
#define PLATFORM_RK3562   1  /* Rockchip RK3562 - Default */
#endif

/* Validate platform selection */
#if (PLATFORM_IMX6ULL && PLATFORM_RK3562)
#error "Only ONE platform can be enabled at a time!"
#endif

#if (!PLATFORM_IMX6ULL && !PLATFORM_RK3562)
#error "At least ONE platform must be enabled!"
#endif

/* ==========================================================================
 * AI Inference Engine Selection (Auto-selected based on platform/engine)
 * ========================================================================== */

#if PLATFORM_RK3562
    #ifndef AI_ENGINE_RKNN
    #define AI_ENGINE_RKNN   0  /* RK3562 NPU hardware acceleration */
    #endif
    #ifndef AI_ENGINE_MNN
    #define AI_ENGINE_MNN    1  /* CPU-based inference */
    #endif
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
 * Video Encoder Selection (Auto-selected based on platform/engine)
 * ========================================================================== */

#if PLATFORM_RK3562
    #ifndef VIDEO_ENCODER_MPP
    #define VIDEO_ENCODER_MPP   0  /* MPP hardware encoding */
    #endif
    #ifndef VIDEO_ENCODER_SW
    #define VIDEO_ENCODER_SW    1  /* openh264 software encoding */
    #endif
#else
    #define VIDEO_ENCODER_MPP   0
    #define VIDEO_ENCODER_SW    1  /* i.MX6ULL uses openh264 */
#endif

/* ==========================================================================
 * Image Processing Engine Selection (Auto-selected based on platform/engine)
 * ========================================================================== */

#if PLATFORM_RK3562
    #ifndef IMG_PROC_RGA
    #define IMG_PROC_RGA        0  /* RGA hardware acceleration */
    #endif
    #ifndef IMG_PROC_SOFTWARE
    #define IMG_PROC_SOFTWARE   1  /* libyuv/libjpeg-turbo */
    #endif
#else
    #define IMG_PROC_RGA        0
    #define IMG_PROC_SOFTWARE   1  /* i.MX6ULL uses software */
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