#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vision_ai_config.h"
#ifdef __cplusplus
extern "C" {
#endif

// ==============================================
// 配置宏（可根据项目修改）
// ==============================================
#define SD_STORAGE_PATH          "/mnt/sdcard/face_capture"  // SD卡存储目录
#define SD_MAX_SAVE_FILES        200                         // 最大循环保存帧数
#define SD_IMAGE_WIDTH           GLOBAL_VIDEO_WIDTH          // 图像宽度
#define SD_IMAGE_HEIGHT          GLOBAL_VIDEO_HEIGHT         // 图像高度
#define SD_RGB_BPP               3                           // RGB888 像素位数

// ==============================================
// 不透明结构体（封装实现，符合OOP规范）
// ==============================================
typedef struct SdStorage SdStorage_t;

// ==============================================
// 公共API（人脸服务直接调用）
// ==============================================
/**
 * @brief  初始化SD卡存储模块
 * @return 模块句柄，NULL=失败
 */
SdStorage_t *SdStorage_Init(void);

/**
 * @brief  保存RGB原始图像到SD卡（循环覆盖策略）
 * @param  self: 模块句柄
 * @param  rgb_buf: 静态内存池中的RGB图像数据（上层传入，不释放）
 * @return 0=成功，负数=失败
 */
int SdStorage_SaveRgbImage(SdStorage_t *self, const uint8_t *rgb_buf);

/**
 * @brief  安全反初始化（优雅退出专用）
 * @param  self: 模块句柄二级指针，自动置NULL
 */
void SdStorage_Deinit(SdStorage_t **self);

/**
 * @brief  全局优雅退出标记（外部信号设置，模块安全退出）
 */
extern bool g_sd_storage_exit_flag;

#ifdef __cplusplus
}
#endif

#endif // SD_STORAGE_H