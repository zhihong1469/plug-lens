#include "sd_mount.h"
#include <unistd.h>
#include <sys/mount.h>
#include <string.h>
#include <stdlib.h>
#include "config_common.h"
// 全局静态变量（内部状态管理）
static sd_state_t g_sd_state = SD_UNMOUNT;
static char g_sd_root_path[64] = CONFIG_SD_STORAGE_ROOT_PATH;

sd_state_t SdMount_Init(void)
{
    // 已挂载成功，直接返回状态
    if (g_sd_state == SD_MOUNTED) {
        return SD_MOUNTED;
    }

    // 1. 检查SD卡设备节点是否存在
    if (access(CONFIG_SD_DEV_NODE, F_OK) != 0) {
        g_sd_state = SD_MOUNT_FAIL;
        return SD_MOUNT_FAIL;
    }

    // 2. 创建挂载目录
    char mkdir_cmd[128];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "%s%s",
             CONFIG_MKDIR_CMD, g_sd_root_path);
    system(mkdir_cmd);

    // 3. 执行挂载
    int mount_ret = mount(CONFIG_SD_DEV_NODE,
                          g_sd_root_path,
                          CONFIG_SD_FS_TYPE,
                          0,
                          NULL);

    // 4. 更新挂载状态
    if (mount_ret == 0) {
        g_sd_state = SD_MOUNTED;
    } else {
        g_sd_state = SD_MOUNT_FAIL;
    }

    return g_sd_state;
}

sd_state_t SdMount_GetState(void)
{
    return g_sd_state;
}

const char *SdMount_GetRootPath(void)
{
    return g_sd_root_path;
}

void SdMount_Deinit(void)
{
    // 仅已挂载时执行卸载
    if (g_sd_state == SD_MOUNTED) {
        umount(g_sd_root_path);
    }

    // 重置状态
    g_sd_state = SD_UNMOUNT;
}