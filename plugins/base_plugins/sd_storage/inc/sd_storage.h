/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           sd_storage.h
 * @brief          SD卡存储服务模块头文件
 * @details        支持RGB原始数据读写 + 标准JPEG图片存储
 *                 工业级存储规范，NFS/SD卡实时可见
 * @author         Luo
 * @date           2026
 ******************************************************************************
 */
#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vision_ai_config.h"

// 存储路径配置
#define SD_STORAGE_ROOT_PATH           CONFIG_SD_STORAGE_ROOT_PATH
#define SD_STORAGE_DIR                 CONFIG_SD_STORAGE_DIR // debug use "/mnt/nfs/face_capture"
// 最大存储文件数量，超出自动覆盖旧文件
#define SD_MAX_CAPTURE_FILES           200

// 图像参数配置
#define SD_INPUT_WIDTH                    GLOBAL_VIDEO_WIDTH
#define SD_INPUT_HEIGHT                   GLOBAL_VIDEO_HEIGHT
#define SD_RGB_IMAGE_SIZE              (SD_INPUT_WIDTH * SD_INPUT_HEIGHT * 3)

// 存储模块错误码
#define SD_STORAGE_OK                  0
#define SD_STORAGE_ERR_PARAM           -1
#define SD_STORAGE_ERR_NOT_INIT        -2
#define SD_STORAGE_ERR_FILE            -3
#define SD_STORAGE_ERR_NO_MEM          -4
#define SD_STORAGE_ERR_MOUNT           -5

// 不透明结构体，外部禁止直接访问成员
typedef struct SdStorage SdStorage_t;

/**
 * @brief  SD卡存储模块初始化
 * @return 成功返回句柄，失败返回NULL
 */
SdStorage_t *SdStorage_Init(void);

/**
 * @brief  反初始化SD卡存储模块，释放资源
 * @param  self: 存储模块句柄
 */
void SdStorage_Deinit(SdStorage_t *self);

/**
 * @brief  保存RGB原始数据到SD卡（原有接口，完整保留）
 * @param  self: 存储模块句柄
 * @param  rgb_buf: RGB数据缓冲区
 * @return 状态码
 */
int SdStorage_SaveRgb(SdStorage_t *self, const uint8_t *rgb_buf);

/**
 * @brief  从SD卡读取RGB原始数据（原有接口，完整保留）
 * @param  self: 存储模块句柄
 * @param  filename: 文件名
 * @param  out_buf: 输出数据缓冲区
 * @param  buf_size: 缓冲区大小
 * @return 状态码
 */
int SdStorage_ReadRgb(SdStorage_t *self, const char *filename, uint8_t *out_buf, size_t buf_size);

/**
 * @brief  保存标准JPEG图片到SD卡（新增接口，复用推流TurboJPEG）
 * @param  self: 存储模块句柄
 * @param  rgb_buf: RGB原始数据
 * @return 状态码
 */
int SdStorage_SaveJpeg(SdStorage_t *self, const uint8_t *rgb_buf);

/**
 * @brief  获取SD卡剩余空间（MB）（原有接口，完整保留）
 * @return 剩余空间大小
 */
long long SdStorage_GetFreeSpaceMB(void);

#endif