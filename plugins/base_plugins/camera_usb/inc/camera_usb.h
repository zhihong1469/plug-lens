#ifndef __CAMERA_USB_H__
#define __CAMERA_USB_H__

#include "camera_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief USB 摄像头创建（唯一对外接口）
 * @param dev_path  /dev/video0
 * @param width     宽
 * @param height    高
 * @param fmt       像素格式 V4L2_PIX_FMT_YUYV / MJPEG 等
 * @param fps       帧率
 * @return 基类指针
 */
camera_base_t *camera_usb_create(const char *dev_path,
                                 int width,
                                 int height,
                                 uint32_t fmt,
                                 uint32_t fps);

/**
 * @brief 销毁 USB 摄像头（自动反初始化）
 */
void camera_usb_destroy(camera_base_t *base_me);

#ifdef __cplusplus
}
#endif

#endif