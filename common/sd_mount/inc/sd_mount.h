/* SPDX-License-Identifier: MIT */
/**
 * @file    sd_mount.h
 * @brief   SD card mount management component for embedded Linux
 * @details Core capabilities:
 *          1. Automatic SD card detection and mounting
 *          2. Mount directory creation and state management
 *          3. Standard mount/umount system call integration
 *          4. Configurable parameters via config_common.h
 *          5. Lightweight state machine for SD status
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Target device: /dev/mmcblk0p1 (SD card partition)
 *          Filesystem: vfat, thread-safe public APIs
 */
#ifndef __SD_MOUNT_H
#define __SD_MOUNT_H

#include <stdbool.h>
#include "config_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ===================== Configuration Macros (Configurable) =====================
/** SD card device node (hardware parameter) */
#define CONFIG_SD_DEV_NODE          "/dev/mmcblk0p1"
/** Default filesystem type for SD card */
#define CONFIG_SD_FS_TYPE           "vfat"
/** Command prefix for creating mount directory */
#define CONFIG_MKDIR_CMD            "mkdir -p "

// ===================== SD Card State Enumeration =====================
/**
 * @brief   SD card mounting status enumeration
 */
typedef enum {
    SD_UNMOUNT    = 0,  /**< SD card unmounted */
    SD_MOUNTED    = 1,  /**< SD card mounted successfully */
    SD_MOUNT_FAIL = 2   /**< SD card mounting failed */
} sd_state_t;

// ===================== Public APIs =====================
/**
 * @brief   Initialize and mount SD card (global single call)
 * @return  Current SD card state
 * @note    Creates mount directory and executes mount system call
 */
sd_state_t SdMount_Init(void);

/**
 * @brief   Get current SD card mount state
 * @return  SD card state enumeration
 */
sd_state_t SdMount_GetState(void);

/**
 * @brief   Get SD card mount root path
 * @return  Read-only root path string
 */
const char *SdMount_GetRootPath(void);

/**
 * @brief   Unmount SD card and deinitialize
 * @note    Executes umount system call and resets state
 */
void SdMount_Deinit(void);

#ifdef __cplusplus
}
#endif

#endif