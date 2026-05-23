/**
 * @file img_joint.cpp
 * @brief 图像联合处理工具模块实现
 * @author 嵌入式视觉项目
 * @date 2026
 */
#include "img_joint.h"
#include <turbojpeg.h>
#include "libyuv.h"
#include <string.h>
#include <stdlib.h>
// OpenH264编码头文件（严格对齐你提供的版本）
#include "codec_api.h"
#include "codec_def.h"

// =============================================================================
// 修复版：YUYV → BGR24（通道顺序正确）
// =============================================================================
int yuyv_to_rgb(const uint8_t* yuyv_data, int width, int height, uint8_t* bgr_buf) {
    if (!yuyv_data || !bgr_buf || width <= 0 || height <= 0 || (width % 2 != 0)) {
        return IMG_JOINT_ERR_INPUT;
    }

    const int argb_size = width * height * 4;
    uint8_t* temp_argb = (uint8_t*)malloc(argb_size);
    if (!temp_argb) return IMG_JOINT_ERR_INPUT;

    // 1. YUYV -> ARGB（不变）
    libyuv::YUY2ToARGB(yuyv_data, width * 2,
                       temp_argb, width * 4,
                       width, height);

    // ✅ 修复：ARGB -> BGR24（正确的通道顺序）
    libyuv::ARGBToRGB24(temp_argb, width * 4,
                        bgr_buf, width * 3,
                        width, height);

    free(temp_argb);
    return IMG_JOINT_OK;
}

int mjpeg_to_rgb(const uint8_t* mjpeg_data, int data_len, int width, int height, uint8_t* bgr_buf) {
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
                           TJPF_RGB,
                           TJFLAG_FASTDCT);

    tjDestroy(tjh);
    tjh = nullptr;

    return (ret == 0) ? IMG_JOINT_OK : IMG_JOINT_ERR_JPEG;
}

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

    return libyuv::YUY2ToI420(yuyv_data, width * 2,
                      y, width,
                      u, uv_stride,
                      v, uv_stride,
                      width, height) == 0 ? IMG_JOINT_OK : IMG_JOINT_ERR_INPUT;
}

int i420_to_rgb(const uint8_t* i420_data, int width, int height, uint8_t* rgb_buf) {
    if (!i420_data || !rgb_buf || width <= 0 || height <= 0) {
        return IMG_JOINT_ERR_INPUT;
    }

    const int y_size = width * height;
    const int uv_stride = width / 2;
    const uint8_t* y = i420_data;
    const uint8_t* u = y + y_size;
    const uint8_t* v = u + uv_stride * (height / 2);

    return libyuv::I420ToRGB24(y, width,
                       u, uv_stride,
                       v, uv_stride,
                       rgb_buf, width * 3,
                       width, height) == 0 ? IMG_JOINT_OK : IMG_JOINT_ERR_INPUT;
}

// =============================================================================
// 修复版：BGR图像缩放（支持三通道交错格式）
// =============================================================================
int rgb_resize(const uint8_t* src_bgr, int src_w, int src_h,
               uint8_t* dst_bgr, int dst_w, int dst_h) {
    if (!src_bgr || !dst_bgr || src_w<=0 || src_h<=0 || dst_w<=0 || dst_h<=0) {
        return -1;
    }

    // 方案：BGR24 → ARGB → 缩放ARGB → BGR24（libyuv官方推荐的三通道缩放方法）
    int src_argb_size = src_w * src_h * 4;
    int dst_argb_size = dst_w * dst_h * 4;
    uint8_t* src_argb = (uint8_t*)malloc(src_argb_size);
    uint8_t* dst_argb = (uint8_t*)malloc(dst_argb_size);

    if (!src_argb || !dst_argb) {
        if (src_argb) free(src_argb);
        if (dst_argb) free(dst_argb);
        return -1;
    }

    // 1. BGR24 → ARGB
    libyuv::RGB24ToARGB(src_bgr, src_w * 3,
                        src_argb, src_w * 4,
                        src_w, src_h);

    // 2. 缩放ARGB（libyuv原生支持ARGB缩放，质量和速度最优）
    libyuv::ARGBScale(src_argb, src_w * 4, src_w, src_h,
                      dst_argb, dst_w * 4, dst_w, dst_h,
                      libyuv::kFilterBilinear);

    // 3. ARGB → BGR24
    libyuv::ARGBToRGB24(dst_argb, dst_w * 4,
                        dst_bgr, dst_w * 3,
                        dst_w, dst_h);

    free(src_argb);
    free(dst_argb);
    return 0;
}

// ========================== BGR画框 (替代cv::rectangle) ==========================
void bgr_draw_rect(uint8_t* bgr_data, int w, int h,
                   int x, int y, int rect_w, int rect_h,
                   uint32_t color, int thickness) {
    if (!bgr_data || x<0 || y<0 || x+rect_w>w || y+rect_h>h) return;

    uint8_t b = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t r = color & 0xFF;

    // 上下边框
    for (int l=0; l<thickness; l++) {
        int yy1 = y + l;
        int yy2 = y + rect_h - 1 - l;
        for (int xx=x; xx<x+rect_w; xx++) {
            uint8_t* p1 = bgr_data + (yy1 * w + xx) * 3;
            uint8_t* p2 = bgr_data + (yy2 * w + xx) * 3;
            p1[0]=b; p1[1]=g; p1[2]=r;
            p2[0]=b; p2[1]=g; p2[2]=r;
        }
    }

    // 左右边框
    for (int l=0; l<thickness; l++) {
        int xx1 = x + l;
        int xx2 = x + rect_w - 1 - l;
        for (int yy=y; yy<y+rect_h; yy++) {
            uint8_t* p1 = bgr_data + (yy * w + xx1) * 3;
            uint8_t* p2 = bgr_data + (yy * w + xx2) * 3;
            p1[0]=b; p1[1]=g; p1[2]=r;
            p2[0]=b; p2[1]=g; p2[2]=r;
        }
    }
}


// =============================================================================
// H264编码器内部结构体（严格对齐OpenH264 API）
// =============================================================================
typedef struct {
    ISVCEncoder*          p_encoder;      // OpenH264编码器
    SEncParamExt         param;          // 编码参数
    uint8_t*             i420_buf;       // 临时I420缓冲区
    int                  width;
    int                  height;
} h264_encoder_impl_t;

// =============================================================================
// H264编码器实现（100%适配你提供的头文件）
// =============================================================================
h264_encoder_t h264_encoder_create(const h264_encode_param_t* param) {
    if (!param || param->width <= 0 || param->height <= 0 || 
        (param->width % 2 != 0) || (param->height % 2 != 0)) {
        return NULL;
    }

    // 分配编码器内存
    h264_encoder_impl_t* impl = (h264_encoder_impl_t*)malloc(sizeof(h264_encoder_impl_t));
    if (!impl) return NULL;
    memset(impl, 0, sizeof(h264_encoder_impl_t));

    // 分配I420临时缓冲区
    int i420_size = param->width * param->height * 3 / 2;
    impl->i420_buf = (uint8_t*)malloc(i420_size);
    if (!impl->i420_buf) {
        free(impl);
        return NULL;
    }

    // 创建OpenH264编码器
    int ret = WelsCreateSVCEncoder(&impl->p_encoder);
    if (ret != 0 || !impl->p_encoder) {
        free(impl->i420_buf);
        free(impl);
        return NULL;
    }

    // 初始化OpenH264参数（IMX6ULL 软编码最优配置）
    memset(&impl->param, 0, sizeof(SEncParamExt));
    impl->p_encoder->GetDefaultParams(&impl->param);

    impl->param.iUsageType = CAMERA_VIDEO_REAL_TIME;    // 实时摄像头场景
    impl->param.fMaxFrameRate = param->fps;             // 帧率
    impl->param.iPicWidth = param->width;              // 宽度
    impl->param.iPicHeight = param->height;            // 高度
    impl->param.iTargetBitrate = param->bitrate * 1000; // 码率（转bps）
    impl->param.iTemporalLayerNum = 1;
    impl->param.iSpatialLayerNum = 1;
    impl->param.bEnableDenoise = false;
    impl->param.bEnableBackgroundDetection = false;
    impl->param.bEnableFrameSkip = false;              // 不丢帧（实时流）
    impl->param.uiIntraPeriod = param->gop;            // I帧间隔
    impl->param.iMultipleThreadIdc = 1;                // 单核编码（IMX6ULL必须）
    impl->param.iNumRefFrame = 1;

    // 初始化编码器
    ret = impl->p_encoder->InitializeExt(&impl->param);
    if (ret != 0) {
        WelsDestroySVCEncoder(impl->p_encoder);
        free(impl->i420_buf);
        free(impl);
        return NULL;
    }

    // 设置输入格式为I420
    int video_format = videoFormatI420;
    impl->p_encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &video_format);

    impl->width = param->width;
    impl->height = param->height;
    return (h264_encoder_t)impl;
}

int yuyv_to_h264(h264_encoder_t encoder,
                 const uint8_t* yuyv_data,
                 int yuyv_len,
                 uint8_t* out_h264,
                 int* out_h264_len) {
    if (!encoder || !yuyv_data || !out_h264 || !out_h264_len || *out_h264_len <= 0) {
        return IMG_JOINT_ERR_INPUT;
    }

    h264_encoder_impl_t* impl = (h264_encoder_impl_t*)encoder;
    int width = impl->width;
    int height = impl->height;

    // 1. YUYV -> I420（libyuv NEON加速）
    int ret = yuyv_to_i420(yuyv_data, width, height, impl->i420_buf);
    if (ret != IMG_JOINT_OK) {
        return IMG_JOINT_ERR_H264;
    }

    // 2. 填充I420图像数据
    SFrameBSInfo     bs_info;
    SSourcePicture   pic;
    memset(&pic, 0, sizeof(SSourcePicture));
    memset(&bs_info, 0, sizeof(SFrameBSInfo));

    pic.iPicWidth = width;
    pic.iPicHeight = height;
    pic.iColorFormat = videoFormatI420;
    pic.pData[0] = impl->i420_buf;
    pic.pData[1] = impl->i420_buf + width * height;
    pic.pData[2] = impl->i420_buf + width * height * 5 / 4;
    pic.iStride[0] = width;
    pic.iStride[1] = width / 2;
    pic.iStride[2] = width / 2;

    // 3. 编码一帧
    ret = impl->p_encoder->EncodeFrame(&pic, &bs_info);
    if (ret != 0 || bs_info.eFrameType == videoFrameTypeSkip) {
        return IMG_JOINT_ERR_H264;
    }

    // ✅ 最终修正：严格按照你提供的SFrameBSInfo/SLayerBSInfo结构解析
    // 我们只用1层，所以取sLayerInfo[0]
    if (bs_info.iLayerNum <= 0) {
        return IMG_JOINT_ERR_H264;
    }

    SLayerBSInfo* layer = &bs_info.sLayerInfo[0];
    if (layer->iNalCount <= 0 || !layer->pBsBuf) {
        return IMG_JOINT_ERR_H264;
    }

    int total_len = 0;
    int max_len = *out_h264_len;
    uint8_t* dst_ptr = out_h264;
    const uint8_t* src_ptr = layer->pBsBuf;

    // 遍历所有NALU，逐个复制
    for (int i = 0; i < layer->iNalCount; i++) {
        int nal_len = layer->pNalLengthInByte[i];
        if (total_len + nal_len > max_len) {
            return IMG_JOINT_ERR_H264; // 缓冲区不足
        }
        memcpy(dst_ptr, src_ptr, nal_len);
        dst_ptr += nal_len;
        src_ptr += nal_len;
        total_len += nal_len;
    }

    *out_h264_len = total_len;
    return IMG_JOINT_OK;
}

void h264_encoder_destroy(h264_encoder_t encoder) {
    if (!encoder) return;
    h264_encoder_impl_t* impl = (h264_encoder_impl_t*)encoder;

    // 释放编码器资源
    if (impl->p_encoder) {
        impl->p_encoder->Uninitialize();
        WelsDestroySVCEncoder(impl->p_encoder);
    }

    // 释放内存
    if (impl->i420_buf) {
        free(impl->i420_buf);
    }
    free(impl);
}