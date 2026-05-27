#ifndef __SD_MOUNT_H
#define __SD_MOUNT_H

#include <stdbool.h>
#include "config_common.h"

// ===================== 【宏配置：全部可手动修改，无硬编码】 =====================
// SD卡设备节点 (核心硬件参数)
#define CONFIG_SD_DEV_NODE          "/dev/mmcblk0p1"
// SD卡默认文件系统类型
#define CONFIG_SD_FS_TYPE           "vfat"
// 挂载命令前缀
#define CONFIG_MKDIR_CMD            "mkdir -p "

// SD卡状态枚举
typedef enum {
    SD_UNMOUNT    = 0,  // 未挂载
    SD_MOUNTED    = 1,  // 已挂载成功
    SD_MOUNT_FAIL = 2   // 挂载失败
} sd_state_t;

// ===================== 对外接口（保持不变） =====================
// 初始化并挂载SD卡（全局唯一调用）
sd_state_t SdMount_Init(void);

// 获取当前SD卡挂载状态
sd_state_t SdMount_GetState(void);

// 获取SD卡根目录路径
const char *SdMount_GetRootPath(void);

// 卸载SD卡，反初始化
void SdMount_Deinit(void);

#endif