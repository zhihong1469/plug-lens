#ifndef __CAMERA_USB_PLUGIN_H__
#define __CAMERA_USB_PLUGIN_H__

// 包含核心设备基类头文件（路径严格匹配你的项目结构）
#include "camera_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief USB摄像头对象构造函数（对外唯一创建接口）
 * @param dev_path  设备节点 /dev/video0
 * @param width     分辨率宽
 * @param height    分辨率高
 * @return camera_base_t* 基类指针（向上转型）
 */
camera_base_t *camera_usb_create(const char *dev_path, int width, int height);

/**
 * @brief USB摄像头对象析构函数
 * @param me 基类指针
 */
void camera_usb_destroy(camera_base_t *me);

#ifdef __cplusplus
}
#endif

#endif // __CAMERA_USB_PLUGIN_H__