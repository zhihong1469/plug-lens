/**
 * @file img_joint.h
 * @brief 图像联合处理工具模块（通用格式转换、编解码）
 * @details 抽离所有跨模块通用的图像工具函数，支持C/C++调用
 * @author 嵌入式视觉项目
 * @date 2026
 */
#ifndef __IMG_JOINT_H__
#define __IMG_JOINT_H__

#include <stdint.h>

// 与项目全局错误码完全对齐
#define IMG_JOINT_OK             0
#define IMG_JOINT_ERR_INPUT     -1
#define IMG_JOINT_ERR_JPEG      -5  // JPEG编解码错误

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief YUYV(YUV422) 转 BGR888（OpenCV实现，兼容旧逻辑）
 * @param yuyv_data: 输入YUYV数据缓冲区
 * @param width: 图像宽度
 * @param height: 图像高度
 * @param bgr_buf: 输出BGR888缓冲区（大小 >= width*height*3）
 * @return 0:成功 / 负数:失败
 */
int yuyv_to_bgr(const uint8_t* yuyv_data, int width, int height, uint8_t* bgr_buf);

/**
 * @brief MJPEG 硬解码转 BGR888（TurboJPEG实现，高性能）
 * @param mjpeg_data: 输入MJPEG压缩数据
 * @param data_len: MJPEG数据长度（字节）
 * @param width: 图像宽度
 * @param height: 图像高度
 * @param bgr_buf: 输出BGR888缓冲区（大小 >= width*height*3）
 * @return 0:成功 / 负数:失败
 */
int mjpeg_to_bgr(const uint8_t* mjpeg_data, int data_len, int width, int height, uint8_t* bgr_buf);

/**
 * @brief YUYV(YUV422) 转 I420(YUV420P)（libyuv NEON加速，H.264编码专用）
 * @param yuyv_data: 输入YUYV数据缓冲区
 * @param width: 图像宽度（必须为偶数）
 * @param height: 图像高度（必须为偶数）
 * @param i420_buf: 输出I420缓冲区（大小 >= width*height*3/2）
 * @return 0:成功 / 负数:失败
 */
int yuyv_to_i420(const uint8_t* yuyv_data, int width, int height, uint8_t* i420_buf);

/**
 * @brief I420(YUV420P) 转 RGB888（libyuv NEON加速）
 * @param i420_data: 输入I420数据缓冲区
 * @param width: 图像宽度
 * @param height: 图像高度
 * @param rgb_buf: 输出RGB888缓冲区（大小 >= width*height*3）
 * @return 0:成功 / 负数:失败
 */
int i420_to_rgb(const uint8_t* i420_data, int width, int height, uint8_t* rgb_buf);

#ifdef __cplusplus
}
#endif

#endif // __IMG_JOINT_H__