/**
 * @file img_joint.h
 * @brief 图像联合处理工具模块（通用格式转换、编解码）
 * @details 抽离所有跨模块通用的图像工具函数，支持C/C++调用
 *          基于libyuv/TurboJPEG/OpenH264实现，无OpenCV依赖
 * @author 嵌入式视觉项目
 * @date 2026
 */
#ifndef __IMG_JOINT_H__
#define __IMG_JOINT_H__

#include <stdint.h>
#include <stdbool.h>
#include "mem_adapter.h"
// 与项目全局错误码完全对齐
#define IMG_JOINT_OK             0
#define IMG_JOINT_ERR_INPUT     -1
#define IMG_JOINT_ERR_JPEG      -5  // JPEG编解码错误
#define IMG_JOINT_ERR_H264      -6  // H264编码错误

// H264编码器句柄（对外隐藏实现）
typedef void* h264_encoder_t;

/**
 * @brief H264编码参数配置（IMX6ULL 实时流最优配置）
 */
typedef struct {
    int     width;          // 输入图像宽度（必须偶数）
    int     height;         // 输入图像高度（必须偶数）
    int     fps;            // 帧率（15~30，IMX6ULL推荐15）
    int     bitrate;        // 码率（kbps，320*240推荐300~500）
    int     gop;            // 关键帧间隔（I帧间隔，推荐15）
    bool    use_cpu_core;   // 单核编码（IMX6ULL必须true）
} h264_encode_param_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief YUYV(YUV422) 转 RGB888（libyuv NEON加速）
 * @param yuyv_data: 输入YUYV数据缓冲区
 * @param width: 图像宽度
 * @param height: 图像高度
 * @param rgb_buf: 输出RGB888缓冲区（大小 >= width*height*3）
 * @return 0:成功 / 负数:失败
 */
int yuyv_to_rgb(const uint8_t* yuyv_data, int width, int height, uint8_t* rgb_buf);

/**
 * @brief MJPEG 硬解码转 RGB888（TurboJPEG实现，高性能）
 * @param mjpeg_data: 输入MJPEG压缩数据
 * @param data_len: MJPEG数据长度（字节）
 * @param width: 图像宽度
 * @param height: 图像高度
 * @param rgb_buf: 输出RGB888缓冲区（大小 >= width*height*3）
 * @return 0:成功 / 负数:失败
 */
int mjpeg_to_rgb(const uint8_t* mjpeg_data, int data_len, int width, int height, uint8_t* rgb_buf);

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

/**
 * @brief RGB图像缩放 (libyuv 双线性滤波，替代OpenCV resize)
 * @param src_rgb: 源RGB888图像数据
 * @param src_w: 源图像宽度
 * @param src_h: 源图像高度
 * @param dst_rgb: 目标RGB888图像数据
 * @param dst_w: 目标图像宽度
 * @param dst_h: 目标图像高度
 * @return 0:成功 / -1:失败
 */
int rgb_resize(const uint8_t* src_rgb, int src_w, int src_h,
               uint8_t* dst_rgb, int dst_w, int dst_h);

/**
 * @brief RGB图像绘制矩形框（裸指针实现，替代OpenCV rectangle）
 * @param color RGB颜色值：0xFF0000=红, 0x00FF00=绿, 0x0000FF=蓝
 * @param thickness: 边框粗细
 */
void bgr_draw_rect(uint8_t* rgb_data, int width, int height,
                   int x, int y, int w, int h, uint32_t color, int thickness);

// ========================= 新增：YUYV -> H264 编码接口 =========================
/**
 * @brief 创建H264编码器
 * @param param 编码参数
 * @return 编码器句柄 / NULL（失败）
 */
h264_encoder_t h264_encoder_create(const h264_encode_param_t* param);

/**
 * @brief 编码一帧YUYV数据为H264
 * @param encoder 编码器句柄
 * @param yuyv_data 输入YUYV原始数据
 * @param yuyv_len YUYV数据长度
 * @param out_h264 输出H264码流缓冲区
 * @param out_h264_len 输入：缓冲区最大长度；输出：实际编码长度
 * @return 0成功 / 负数失败
 */
int yuyv_to_h264(h264_encoder_t encoder,
                 const uint8_t* yuyv_data,
                 int yuyv_len,
                 uint8_t* out_h264,
                 int* out_h264_len);
// =============================================================================
// 【官方标准】获取H264 SPS/PPS（EncodeParameterSets 官方接口）
// =============================================================================
int h264_encoder_get_sps_pps(h264_encoder_t encoder, uint8_t* sps_pps_buf, int* buf_len) ;
/**
 * @brief 销毁H264编码器
 * @param encoder 编码器句柄
 */
void h264_encoder_destroy(h264_encoder_t encoder);

/*
#include "img_joint.h"
#include "rtsp_server.h"

#define CAM_W 640
#define CAM_H 360

int main(void) {
    // 1. 配置编码器
    h264_encode_param_t param = {
        .width = CAM_W,
        .height = CAM_H,
        .fps = 15,
        .bitrate = 500,
        .gop = 15,
        .use_cpu_core = true
    };

    // 2. 创建编码器
    h264_encoder_t encoder = h264_encoder_create(&param);
    if (!encoder) {
        printf("H264 encoder create failed\n");
        return -1;
    }

    // 3. 启动RTSP服务
    rtsp_server_start();

    // 4. 准备缓冲区
    uint8_t yuyv_buf[CAM_W * CAM_H * 2];
    uint8_t h264_buf[CAM_W * CAM_H]; // 足够大的缓冲区
    int h264_len;

    // 5. 实时编码推流
    while (1) {
        // 从摄像头获取YUYV数据
        get_camera_yuyv(yuyv_buf);

        // 编码
        h264_len = sizeof(h264_buf);
        int ret = yuyv_to_h264(encoder, yuyv_buf, sizeof(yuyv_buf), h264_buf, &h264_len);
        if (ret == IMG_JOINT_OK) {
            // 推流
            rtsp_server_push(buf, len);
        }
    }

    // 6. 清理资源
    h264_encoder_destroy(encoder);
    rtsp_server_stop();
    return 0;
}
*/
#ifdef __cplusplus
}
#endif

#endif // __IMG_JOINT_H__