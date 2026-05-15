#ifndef __CAMERA_USB_H__
#define __CAMERA_USB_H__

#include "camera_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file camera_usb.h
 * @brief USB摄像头子类头文件（V3.0 封装版）
 * @details 继承camera_base基类，实现USB摄像头专用驱动
 *          私有实现完全封装，对外仅暴露2个标准接口
 *          硬件缓冲区管理内部闭环，上层无需要关心底层细节
 * @version 3.0
 * @date 2025
 */

/**
 * @brief USB摄像头实例创建（唯一对外构造接口）
 * @param dev_path 设备节点路径（如：/dev/video0）
 * @param width 期望图像宽度
 * @param height 期望图像高度
 * @param fmt 像素格式（V4L2_PIX_FMT_YUYV/MJPEG等）
 * @param fps 期望帧率
 * @return camera_base_t* 成功返回基类指针，失败返回NULL
 * @note 内部完成子类内存分配、基类初始化、参数配置
 *       无需关心底层私有实现
 */
camera_base_t *camera_usb_create(const char *dev_path,
                                 int width,
                                 int height,
                                 uint32_t fmt,
                                 uint32_t fps);

/**
 * @brief 销毁USB摄像头实例（唯一对外析构接口）
 * @param base_me 摄像头基类指针
 * @details 自动执行：停止采集 → 反初始化 → 释放内存
 *          上层调用后，指针置空，避免野指针
 */
void camera_usb_destroy(camera_base_t *base_me);

#ifdef __cplusplus
}
#endif

#endif