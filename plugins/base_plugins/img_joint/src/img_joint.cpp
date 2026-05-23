#include "img_joint.h"
#include <opencv2/opencv.hpp>
#include <turbojpeg.h>
#include "libyuv.h"

// =============================================================================
// YUYV 转 BGR888（OpenCV实现，完全保留原有逻辑）
// =============================================================================
int yuyv_to_bgr(const uint8_t* yuyv_data, int width, int height, uint8_t* bgr_buf) {
    if (!yuyv_data || !bgr_buf || width <= 0 || height <= 0) {
        return IMG_JOINT_ERR_INPUT;
    }

    cv::Mat yuyv_mat(height, width, CV_8UC2, (void*)yuyv_data);
    cv::Mat bgr_mat(height, width, CV_8UC3, bgr_buf);
    cv::cvtColor(yuyv_mat, bgr_mat, cv::COLOR_YUV2BGR_YUYV);

    return IMG_JOINT_OK;
}

// =============================================================================
// MJPEG 硬解码转 BGR888（TurboJPEG实现，完全保留原有逻辑）
// =============================================================================
int mjpeg_to_bgr(const uint8_t* mjpeg_data, int data_len, int width, int height, uint8_t* bgr_buf) {
    if (!mjpeg_data || !bgr_buf || data_len <= 0 || width <= 0 || height <= 0) {
        return IMG_JOINT_ERR_INPUT;
    }

    tjhandle tjh = tjInitDecompress();
    if (!tjh) {
        return IMG_JOINT_ERR_JPEG;
    }

    int ret = tjDecompress2(tjh,
                           mjpeg_data,
                           data_len,
                           bgr_buf,
                           width,
                           0,
                           height,
                           TJPF_BGR,
                           TJFLAG_FASTDCT);

    tjDestroy(tjh);
    tjh = nullptr;

    return (ret == 0) ? IMG_JOINT_OK : IMG_JOINT_ERR_JPEG;
}

// =============================================================================
// YUYV 转 I420（libyuv NEON加速，H.264编码专用）
// =============================================================================
int yuyv_to_i420(const uint8_t* yuyv_data, int width, int height, uint8_t* i420_buf) {
    if (!yuyv_data || !i420_buf || width <= 0 || height <= 0 ||
        (width % 2 != 0) || (height % 2 != 0)) {
        return IMG_JOINT_ERR_INPUT;
    }

    const int y_size = width * height;
    const int uv_stride = width / 2;
    uint8_t* y = i420_buf;
    uint8_t* u = y + y_size;
    uint8_t* v = u + uv_stride * (height / 2);

    // ✅ 修复：添加 libyuv:: 命名空间
    return libyuv::YUY2ToI420(yuyv_data, width * 2,
                      y, width,
                      u, uv_stride,
                      v, uv_stride,
                      width, height) == 0 ? IMG_JOINT_OK : IMG_JOINT_ERR_INPUT;
}

// =============================================================================
// I420 转 RGB888（libyuv NEON加速）
// =============================================================================
int i420_to_rgb(const uint8_t* i420_data, int width, int height, uint8_t* rgb_buf) {
    if (!i420_data || !rgb_buf || width <= 0 || height <= 0) {
        return IMG_JOINT_ERR_INPUT;
    }

    const int y_size = width * height;
    const int uv_stride = width / 2;
    const uint8_t* y = i420_data;
    const uint8_t* u = y + y_size;
    const uint8_t* v = u + uv_stride * (height / 2);

    // ✅ 修复：添加 libyuv:: 命名空间
    return libyuv::I420ToRGB24(y, width,
                       u, uv_stride,
                       v, uv_stride,
                       rgb_buf, width * 3,
                       width, height) == 0 ? IMG_JOINT_OK : IMG_JOINT_ERR_INPUT;
}