/**
 * @file img_joint.cpp
 * @brief 图像联合处理工具模块实现
 * @details 基于libyuv/TurboJPEG/OpenH264实现图像格式转换、编解码
 *          无OpenCV依赖，适配IMX6ULL嵌入式平台
 * @author 嵌入式视觉项目
 * @date 2026
 */
#include "img_joint.h"
#include <turbojpeg.h>
#include "libyuv.h"
#include <string.h>
#include <stdlib.h>

// OpenH264编码头文件（严格对齐平台版本 + 官方接口）
#include "codec_api.h"

// 工具宏：向上取整到16字节对齐
#define ALIGN16(x) (((x) + 15) & ~15)

// =============================================================================
// YUYV(YUV422) 转 RGB888（libyuv 标准转换流程）
// =============================================================================
int yuyv_to_rgb(const uint8_t* yuyv_data, int width, int height, uint8_t* rgb_buf) {
    if (!yuyv_data || !rgb_buf || width <= 0 || height <= 0 || (width % 2 != 0)) {
        return IMG_JOINT_ERR_INPUT;
    }

    const int argb_size = width * height * 4;
    uint8_t* temp_argb = (uint8_t*)mem_alloc(argb_size);
    if (!temp_argb) return IMG_JOINT_ERR_INPUT;

    // YUYV -> ARGB 中间转换
    libyuv::YUY2ToARGB(yuyv_data, width * 2,
                       temp_argb, width * 4,
                       width, height);

    // ARGB -> RGB888 标准输出
    libyuv::ARGBToRGB24(temp_argb, width * 4,
                        rgb_buf, width * 3,
                        width, height);

    mem_free(temp_argb);
    return IMG_JOINT_OK;
}

// =============================================================================
// MJPEG硬解码 转 RGB888（TurboJPEG 高性能实现）
// =============================================================================
int mjpeg_to_rgb(const uint8_t* mjpeg_data, int data_len, int width, int height, uint8_t* rgb_buf) {
    if (!mjpeg_data || !rgb_buf || data_len <= 0 || width <= 0 || height <= 0) {
        return IMG_JOINT_ERR_INPUT;
    }

    tjhandle tjh = tjInitDecompress();
    if (!tjh) {
        return IMG_JOINT_ERR_JPEG;
    }

    int ret = tjDecompress2(tjh,
                           mjpeg_data,
                           data_len,
                           rgb_buf,
                           width,
                           0,
                           height,
                           TJPF_RGB,
                           TJFLAG_FASTDCT);

    tjDestroy(tjh);
    tjh = nullptr;

    return (ret == 0) ? IMG_JOINT_OK : IMG_JOINT_ERR_JPEG;
}

// =============================================================================
// YUYV(YUV422) 转 I420(YUV420P)（H.264编码专用格式）
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

    return libyuv::YUY2ToI420(yuyv_data, width * 2,
                      y, width,
                      u, uv_stride,
                      v, uv_stride,
                      width, height) == 0 ? IMG_JOINT_OK : IMG_JOINT_ERR_INPUT;
}

// =============================================================================
// I420(YUV420P) 转 RGB888
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

    return libyuv::I420ToRGB24(y, width,
                       u, uv_stride,
                       v, uv_stride,
                       rgb_buf, width * 3,
                       width, height) == 0 ? IMG_JOINT_OK : IMG_JOINT_ERR_INPUT;
}

// =============================================================================
// 兼容原接口：libyuv RGB缩放（无动态malloc，等比例裁剪，模型输入专用）
// 【适配版】仅使用你库中存在的接口：RGB24ToARGB / ARGBScale / ARGBCopy / ARGBToRGB24
// =============================================================================
int rgb_resize(const uint8_t* src_rgb, int src_w, int src_h,
               uint8_t* dst_rgb, int dst_w, int dst_h) {
    if (!src_rgb || !dst_rgb || src_w<=0 || src_h<=0 || dst_w<=0 || dst_h<=0) {
        return -1;
    }

    // 静态缓冲区，按16字节对齐分配
    static uint8_t argb_buf[ALIGN16(640) * 360 * 4];
    static uint8_t crop_argb_buf[ALIGN16(160) * 120 * 4];

    // 等比例缩放计算（不变）
    float scale_w = (float)dst_w / src_w;
    float scale_h = (float)dst_h / src_h;
    float scale    = (scale_w > scale_h) ? scale_w : scale_h;

    int scaled_w = (int)(src_w * scale);
    int scaled_h = (int)(src_h * scale);
    int crop_x   = (scaled_w - dst_w) / 2;
    int crop_y   = (scaled_h - dst_h) / 2;

    // 计算对齐后的步长（关键修复！）
    int src_rgb_stride = ALIGN16(src_w * 3);
    int src_argb_stride = ALIGN16(src_w * 4);
    int scaled_argb_stride = ALIGN16(scaled_w * 4);
    int dst_argb_stride = ALIGN16(dst_w * 4);
    int dst_rgb_stride = ALIGN16(dst_w * 3);

    // 1. RGB24 -> ARGB（使用对齐后的步长）
    libyuv::RGB24ToARGB(src_rgb, src_rgb_stride,
                        argb_buf, src_argb_stride,
                        src_w, src_h);

    // 2. ARGB 双线性缩放（使用对齐后的步长）
    libyuv::ARGBScale(argb_buf, src_argb_stride, src_w, src_h,
                      argb_buf, scaled_argb_stride, scaled_w, scaled_h,
                      libyuv::kFilterBilinear);

    // 3. 居中裁剪（使用对齐后的步长）
    const uint8_t* src_crop = argb_buf + (crop_y * scaled_argb_stride) + (crop_x * 4);
    libyuv::ARGBCopy(src_crop, scaled_argb_stride,
                     crop_argb_buf, dst_argb_stride,
                     dst_w, dst_h);

    // 4. ARGB -> RGB24（使用对齐后的步长）
    libyuv::ARGBToRGB24(crop_argb_buf, dst_argb_stride,
                        dst_rgb, dst_rgb_stride,
                        dst_w, dst_h);

    return 0;
}

// =============================================================================
// RGB图像绘制矩形框（裸指针操作，无第三方库依赖）
// =============================================================================
void bgr_draw_rect(uint8_t* rgb_data, int w, int h,
                   int x, int y, int rect_w, int rect_h,
                   uint32_t color, int thickness) {
    if (!rgb_data || x<0 || y<0 || x+rect_w>w || y+rect_h>h) return;

    // RGB通道解析
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // 绘制上下边框
    for (int l=0; l<thickness; l++) {
        int yy1 = y + l;
        int yy2 = y + rect_h - 1 - l;
        for (int xx=x; xx<x+rect_w; xx++) {
            uint8_t* p1 = rgb_data + (yy1 * w + xx) * 3;
            uint8_t* p2 = rgb_data + (yy2 * w + xx) * 3;
            p1[0]=r; p1[1]=g; p1[2]=b;
            p2[0]=r; p2[1]=g; p2[2]=b;
        }
    }

    // 绘制左右边框
    for (int l=0; l<thickness; l++) {
        int xx1 = x + l;
        int xx2 = x + rect_w - 1 - l;
        for (int yy=y; yy<y+rect_h; yy++) {
            uint8_t* p1 = rgb_data + (yy * w + xx1) * 3;
            uint8_t* p2 = rgb_data + (yy * w + xx2) * 3;
            p1[0]=r; p1[1]=g; p1[2]=b;
            p2[0]=r; p2[1]=g; p2[2]=b;
        }
    }
}

// =============================================================================
// H264编码器内部结构体（标准OpenH264对齐）
// =============================================================================
typedef struct {
    ISVCEncoder*          p_encoder;
    SEncParamExt          param;
    uint8_t*              i420_buf;
    int                   width;
    int                   height;
} h264_encoder_impl_t;

// =============================================================================
// 标准编码器初始化（全网通用模板）
// 🔥 优化版：优先流畅度/低CPU，适配IMX6ULL单核
// =============================================================================
h264_encoder_t h264_encoder_create(const h264_encode_param_t* param) {
    if (!param || param->width <= 0 || param->height <= 0 || 
        (param->width % 2 != 0) || (param->height % 2 != 0)) {
        return NULL;
    }

    h264_encoder_impl_t* impl = (h264_encoder_impl_t*)mem_alloc(sizeof(h264_encoder_impl_t));
    if (!impl) return NULL;
    memset(impl, 0, sizeof(h264_encoder_impl_t));

    int i420_size = param->width * param->height * 3 / 2;
    impl->i420_buf = (uint8_t*)mem_alloc(i420_size);
    if (!impl->i420_buf) {
        mem_free(impl);
        return NULL;
    }

    int ret = WelsCreateSVCEncoder(&impl->p_encoder);
    if (ret != 0 || !impl->p_encoder) {
        mem_free(impl->i420_buf);
        mem_free(impl);
        return NULL;
    }

    impl->p_encoder->GetDefaultParams(&impl->param);

    // ===================== 核心优化：流畅度优先 + 低算力 =====================
    impl->param.iUsageType      = CAMERA_VIDEO_REAL_TIME;    // 实时通信模式
    impl->param.fMaxFrameRate   = param->fps;
    impl->param.iPicWidth       = param->width;
    impl->param.iPicHeight      = param->height;
    impl->param.iTargetBitrate  = param->bitrate * 1000;
    impl->param.uiIntraPeriod   = 30;                        // 增大IDR周期，减少算力开销
    impl->param.iTemporalLayerNum = 1;
    impl->param.iSpatialLayerNum  = 1;

    // 🔥 最低复杂度模式：最快编码速度（流畅度拉满）
    impl->param.iComplexityMode = LOW_COMPLEXITY;
    // 🔥 单核CPU：关闭多线程，避免抢占
    impl->param.iMultipleThreadIdc = 1;
    // 🔥 最低参考帧：仅1帧，大幅降低算力
    impl->param.iNumRefFrame = 1;
    // 🔥 低算力熵编码：CAVLC（替代高算力CABAC）
    impl->param.iEntropyCodingModeFlag = 0;
    // 🔥 关闭环路滤波：节省大量CPU
    impl->param.iLoopFilterDisableIdc = 1;
    // 🔥 SPS/PPS固定ID：减少开销
    impl->param.eSpsPpsIdStrategy = CONSTANT_ID;

    // 🔥 关闭所有冗余高级功能（全关，省算力）
    impl->param.bEnableDenoise = false;
    impl->param.bEnableBackgroundDetection = false;
    impl->param.bEnableAdaptiveQuant = false;
    impl->param.bEnableSceneChangeDetect = false;
    impl->param.bPrefixNalAddingCtrl = false;
    impl->param.bEnableFrameSkip = true;    // 允许跳帧，保证实时流畅
    impl->param.bIsLosslessLink = false;

    // 初始化编码器
    ret = impl->p_encoder->InitializeExt(&impl->param);
    if (ret != 0) {
        WelsDestroySVCEncoder(impl->p_encoder);
        mem_free(impl->i420_buf);
        mem_free(impl);
        return NULL;
    }

    int fmt = videoFormatI420;
    impl->p_encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &fmt);

    impl->width = param->width;
    impl->height = param->height;
    return (h264_encoder_t)impl;
}


// =============================================================================
// 标准获取SPS/PPS（官方接口，全网通用）
// =============================================================================
int h264_encoder_get_sps_pps(h264_encoder_t encoder, uint8_t* sps_pps_buf, int* buf_len) {
    if (!encoder || !sps_pps_buf || !buf_len || *buf_len < 128)
        return IMG_JOINT_ERR_INPUT;

    h264_encoder_impl_t* impl = (h264_encoder_impl_t*)encoder;
    SFrameBSInfo info = {0};
    
    if (impl->p_encoder->EncodeParameterSets(&info) != 0 || info.iLayerNum == 0)
        return IMG_JOINT_ERR_H264;

    SLayerBSInfo* layer = &info.sLayerInfo[0];
    int total = 0;
    const uint8_t* src = layer->pBsBuf;
    for (int i=0; i<layer->iNalCount; i++) {
        int len = layer->pNalLengthInByte[i];
        memcpy(sps_pps_buf+total, src, len);
        src += len;
        total += len;
    }
    *buf_len = total;
    return IMG_JOINT_OK;
}

// =============================================================================
// 适配OpenH264 v2.1.1的YUV转H264函数（IMX6ULL专用）
// =============================================================================
int yuyv_to_h264(h264_encoder_t encoder,
                 const uint8_t* yuyv_data,
                 int yuyv_len,
                 uint8_t* out_h264,
                 int* out_h264_len) {
    if (!encoder || !yuyv_data || !out_h264 || !out_h264_len)
        return IMG_JOINT_ERR_INPUT;

    h264_encoder_impl_t* impl = (h264_encoder_impl_t*)encoder;
    int w = impl->width, h = impl->height;

    if (yuyv_to_i420(yuyv_data, w, h, impl->i420_buf) != IMG_JOINT_OK)
        return IMG_JOINT_ERR_H264;

    SSourcePicture pic = {0};
    SFrameBSInfo  bs_info = {0};

    pic.iPicWidth    = w;
    pic.iPicHeight   = h;
    pic.iColorFormat = videoFormatI420;
    pic.pData[0]     = impl->i420_buf;
    pic.pData[1]     = impl->i420_buf + w*h;
    pic.pData[2]     = impl->i420_buf + w*h*5/4;
    pic.iStride[0]   = w;
    pic.iStride[1]   = w/2;
    pic.iStride[2]   = w/2;

    // ===================== 强制首帧 IDR =====================
    static bool first_frame = true;
    if (first_frame) {
        impl->p_encoder->ForceIntraFrame(true);
        first_frame = false;
    }

    int ret = impl->p_encoder->EncodeFrame(&pic, &bs_info);
    if (ret != 0 || bs_info.eFrameType == videoFrameTypeSkip)
        return IMG_JOINT_ERR_H264;

    // 拷贝所有NAL数据
    int total_len = 0;
    for (int i = 0; i < bs_info.iLayerNum; i++) {
        SLayerBSInfo* layer = &bs_info.sLayerInfo[i];
        int offset = 0;
        for (int j = 0; j < layer->iNalCount; j++) {
            int nal_len = layer->pNalLengthInByte[j];
            memcpy(out_h264 + total_len, (uint8_t*)layer->pBsBuf + offset, nal_len);
            total_len += nal_len;
            offset += nal_len;
        }
    }

    *out_h264_len = total_len;
    return IMG_JOINT_OK;
}

// =============================================================================
// 标准销毁编码器
// =============================================================================
void h264_encoder_destroy(h264_encoder_t encoder) {
    if (!encoder) return;
    h264_encoder_impl_t* impl = (h264_encoder_impl_t*)encoder;
    if (impl->p_encoder) {
        impl->p_encoder->Uninitialize();
        WelsDestroySVCEncoder(impl->p_encoder);
    }
    mem_free(impl->i420_buf);
    mem_free(impl);
}