/* SPDX-License-Identifier: MIT */
/**
 * @file    sd_mount.c
 * @brief   Implementation of SD card mounting management
 * @details Low-level implementation:
 *          1. Device node existence check
 *          2. Automatic mount directory creation
 *          3. Linux mount()/umount() system call wrapper
 *          4. Global state management for thread-safe access
 *          5. Idempotent initialization (safe repeated calls)
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "sd_mount.h"
#include <unistd.h>
#include <sys/mount.h>
#include <string.h>
#include <stdlib.h>
#include "config_common.h"

// ===================== Global Static Variables =====================
/** Global SD card mount state */
static sd_state_t g_sd_state = SD_UNMOUNT;
/** SD card mount root path buffer */
static char g_sd_root_path[64] = CONFIG_SD_STORAGE_ROOT_PATH;

/**
 * @brief   Initialize SD card mounting workflow
 * @return  SD card final state
 * @details Check device → create directory → mount SD card
 */
sd_state_t SdMount_Init(void)
{
    // Return directly if already mounted (idempotent)
    if (g_sd_state == SD_MOUNTED) {
        return SD_MOUNTED;
    }

    // 1. Check if SD card device node exists
    if (access(CONFIG_SD_DEV_NODE, F_OK) != 0) {
        g_sd_state = SD_MOUNT_FAIL;
        return SD_MOUNT_FAIL;
    }

    // 2. Create mount directory using system command
    char mkdir_cmd[128];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "%s%s",
             CONFIG_MKDIR_CMD, g_sd_root_path);
    system(mkdir_cmd);

    // 3. Execute Linux mount system call
    int mount_ret = mount(CONFIG_SD_DEV_NODE,
                          g_sd_root_path,
                          CONFIG_SD_FS_TYPE,
                          0,
                          NULL);

    // 4. Update mount state
    if (mount_ret == 0) {
        g_sd_state = SD_MOUNTED;
    } else {
        g_sd_state = SD_MOUNT_FAIL;
    }

    return g_sd_state;
}

/**
 * @brief   Get current SD card state
 */
sd_state_t SdMount_GetState(void)
{
    return g_sd_state;
}

/**
 * @brief   Get SD card mount root path
 */
const char *SdMount_GetRootPath(void)
{
    return g_sd_root_path;
}

/**
 * @brief   Unmount SD card and reset state
 */
void SdMount_Deinit(void)
{
    // Only unmount if SD card is mounted
    if (g_sd_state == SD_MOUNTED) {
        umount(g_sd_root_path);
    }

    // Reset state to unmounted
    g_sd_state = SD_UNMOUNT;
}