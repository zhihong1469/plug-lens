#ifndef IMG_STORAGE_H
#define IMG_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vision_ai_config.h"

// 存储路径配置（已适配 /mnt/test 无SD卡模式）
#define IMG_STORAGE_ROOT_PATH           CONFIG_SD_STORAGE_ROOT_PATH
#define IMG_STORAGE_DIR                 CONFIG_SD_STORAGE_DIR // /mnt/test/face_capture
// 最大存储文件数量，超出自动覆盖旧文件
#define IMG_MAX_CAPTURE_FILES           200000

// 图像参数配置
#define IMG_INPUT_WIDTH                    GLOBAL_VIDEO_WIDTH
#define IMG_INPUT_HEIGHT                   GLOBAL_VIDEO_HEIGHT
#define IMG_RGB_IMAGE_SIZE              (IMG_INPUT_WIDTH * IMG_INPUT_HEIGHT * 3)

// 存储模块错误码
#define IMG_STORAGE_OK                  0
#define IMG_STORAGE_ERR_PARAM           -1
#define IMG_STORAGE_ERR_NOT_INIT        -2
#define IMG_STORAGE_ERR_FILE            -3
#define IMG_STORAGE_ERR_NO_MEM          -4

// 不透明结构体，外部禁止直接访问成员
typedef struct ImgStorage ImgStorage_t;

/**
 * @brief  图像存储模块初始化（支持NFS/本地/网络目录，无SD卡依赖）
 * @return 成功返回句柄，失败返回NULL
 */
ImgStorage_t *img_storage_init(void);

/**
 * @brief  反初始化图像存储模块，释放资源
 * @param  self: 存储模块句柄
 */
void img_storage_deinit(ImgStorage_t *self);

/**
 * @brief  保存RGB原始数据（无SD卡依赖，纯文件存储）
 * @param  self: 存储模块句柄
 * @param  rgb_buf: RGB数据缓冲区
 * @return 状态码
 */
int img_storage_save_rgb(ImgStorage_t *self, const uint8_t *rgb_buf);

/**
 * @brief  读取RGB原始数据
 * @param  self: 存储模块句柄
 * @param  filename: 文件名
 * @param  out_buf: 输出数据缓冲区
 * @param  buf_size: 缓冲区大小
 * @return 状态码
 */
int img_storage_read_rgb(ImgStorage_t *self, const char *filename, uint8_t *out_buf, size_t buf_size);

/**
 * @brief  保存标准JPEG图片（复用TurboJPEG编码）
 * @param  self: 存储模块句柄
 * @param  rgb_buf: RGB原始数据
 * @return 状态码
 */
int img_storage_save_jpeg(ImgStorage_t *self, const uint8_t *rgb_buf);

/**
 * @brief  获取存储目录剩余空间（MB）
 * @return 剩余空间大小
 */
long long img_storage_get_free_space_mb(void);

#endif