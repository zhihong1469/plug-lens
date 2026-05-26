#include "ai_model_mnn.hpp"
#include "ai_model_base.h"
#include "UltraFaceMNN.hpp"
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#include "utils.h"
// 新增：OpenCV头文件（已在UltraFaceMNN.hpp包含，这里显式声明）
#include "img_joint.h"
using namespace std;

// ==========================
// MNN 子类私有数据（通用化命名）
// ==========================
typedef struct {
    UltraFaceMNN*            ultra_face;     /**< MNN模型实例 */
    int                      ai_w;           /**< 模型输入宽度 */
    int                      ai_h;           /**< 模型输入高度 */
    const uint8_t*           frame_data;     /**< 通用图像帧数据（兼容YUYV/MJPEG） */
    uint8_t*                 external_bgr_buf;/**< 外部BGR推理缓存 */
    vector<FaceInfo_MNN>     curr_faces;     /**< 当前人脸检测结果 */
} mnn_priv_t;

static ai_model_handle_t* g_mnn_handle = nullptr;
static mnn_priv_t*        g_priv        = nullptr;

// ==========================
// 初始化
// ==========================
static ai_model_err_t mnn_ai_init(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = new(nothrow) mnn_priv_t;
    if (!priv) return AI_MODEL_ERR_NO_MEM;
    memset(priv, 0, sizeof(mnn_priv_t));

    priv->ultra_face = new(nothrow) UltraFaceMNN();
    if (!priv->ultra_face) {
        delete priv;
        return AI_MODEL_ERR_NO_MEM;
    }

    int ret = priv->ultra_face->init(
        handle->config.model_path,
        handle->config.input_width,
        handle->config.input_height,
        handle->config.score_thresh,
        handle->config.iou_thresh
    );

    if (ret != MNN_FACE_OK) {
        delete priv->ultra_face;
        delete priv;
        return AI_MODEL_ERR_INIT;
    }

    priv->ai_w = handle->config.input_width;
    priv->ai_h = handle->config.input_height;
    handle->user_data = priv;
    
    g_priv = priv;
    g_mnn_handle = handle;

    return AI_MODEL_OK;
}

// ==========================
// 输入数据（通用化）
// ==========================
static ai_model_err_t mnn_ai_input(ai_model_handle_t* handle, uint8_t* data, uint32_t len)
{
    if (!handle || !data) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_ERR_INIT;

    // 存储通用图像数据
    priv->frame_data = data;
    return AI_MODEL_OK;
}

// ==========================
// 推理（适配默认格式）
// ==========================
static ai_model_err_t mnn_ai_infer(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv || !priv->frame_data || !priv->external_bgr_buf) {
        return AI_MODEL_ERR_INIT;
    }

    priv->curr_faces.clear();
    // 通用推理默认使用MJPEG格式（硬件最优）
    int ret = priv->ultra_face->detect(
        priv->frame_data,
        priv->ai_w,
        priv->ai_h,
        priv->external_bgr_buf,
        priv->curr_faces,
        IMAGE_FORMAT_MJPEG
    );

    return (ret == MNN_FACE_OK) ? AI_MODEL_OK : AI_MODEL_ERR_INFER;
}

// ==========================
// 获取结果
// ==========================
static ai_model_err_t mnn_ai_get_result(ai_model_handle_t* handle,
                                       ai_model_detect_result_t* results,
                                       uint32_t* result_count)
{
    if (!handle || !results || !result_count) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_ERR_INIT;

    uint32_t num = min(priv->curr_faces.size(), (size_t)10);
    *result_count = num;

    for (uint32_t i = 0; i < num; i++) {
        auto& f = priv->curr_faces[i];
        results[i].x1 = f.x1;
        results[i].y1 = f.y1;
        results[i].x2 = f.x2;
        results[i].y2 = f.y2;
        results[i].score = f.score;
        results[i].class_id = 0;
    }

    return AI_MODEL_OK;
}

// ==========================
// 反初始化
// ==========================
static ai_model_err_t mnn_ai_deinit(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_OK;

    if (priv->ultra_face) {
        priv->ultra_face->deinit();
        delete priv->ultra_face;
    }

    delete priv;
    handle->user_data = nullptr;
    g_priv = nullptr;
    g_mnn_handle = nullptr;

    return AI_MODEL_OK;
}

// ==========================
// 虚函数表
// ==========================
static const ai_model_ops_t g_mnn_ai_ops = {
    .init       = mnn_ai_init,
    .input      = mnn_ai_input,
    .infer      = mnn_ai_infer,
    .get_result = mnn_ai_get_result,
    .deinit     = mnn_ai_deinit,
};

// ==========================
// 创建模型
// ==========================
ai_model_handle_t* ai_model_mnn_create(const ai_model_config_t* config)
{
    return ai_model_create(config, &g_mnn_ai_ops);
}

// ==========================
// 工具接口：坐标映射（原有逻辑，不动）
// ==========================
void ai_model_mnn_map_face(FaceInfo_C* face, int cam_w, int cam_h)
{
    if (!face || !g_priv) return;

    float sw = (float)cam_w / g_priv->ai_w;
    float sh = (float)cam_h / g_priv->ai_h;
    face->x1 *= sw;
    face->y1 *= sh;
    face->x2 *= sw;
    face->y2 *= sh;
}

// ==========================
// 【已修复】100%适配 rgb_resize 等比例裁剪方案
// ==========================
void ai_model_mnn_map_and_draw_faces(FaceInfo_C* faces, int face_num, 
                                     int cam_w, int cam_h,
                                     const uint8_t *src_frame, uint8_t *dst_frame)
{
    if (!faces || face_num <= 0 || !src_frame || !dst_frame || !g_priv) return;

    // 拷贝原图
    memcpy(dst_frame, src_frame, cam_w * cam_h * 3);

    int model_w = g_priv->ai_w;  // 160
    int model_h = g_priv->ai_h;  // 120

    // ==============================================
    // 【修复1】全部使用浮点计算，避免int截断误差
    // 和rgb_resize的计算逻辑完全一致
    // ==============================================
    float scale_w = (float)model_w / cam_w;
    float scale_h = (float)model_h / cam_h;
    float scale    = utils_fmaxf(scale_w, scale_h);

    float scaled_w = cam_w * scale;
    float scaled_h = cam_h * scale;
    float crop_x   = (scaled_w - model_w) / 2.0f;
    float crop_y   = (scaled_h - model_h) / 2.0f;

    const float score_thresh = 0.5f;

    for (int i = 0; i < face_num; i++) 
    {
        FaceInfo_C* face = &faces[i];
        if (face->score < score_thresh) continue;

        // 模型输出已经是模型图像素坐标（0~160, 0~120）
        float model_x1 = face->x1;
        float model_y1 = face->y1;
        float model_x2 = face->x2;
        float model_y2 = face->y2;

        // ==============================================
        // 【修复2】精确逆映射公式（使用浮点crop值）
        // ==============================================
        float x1 = (model_x1 + crop_x) / scale;
        float y1 = (model_y1 + crop_y) / scale;
        float x2 = (model_x2 + crop_x) / scale;
        float y2 = (model_y2 + crop_y) / scale;
    // printf("原始坐标: x1=%.2f, y1=%.2f, x2=%.2f, y2=%.2f, score=%.2f\n", 
    //     face->x1, face->y1, face->x2, face->y2, face->score);
    //     // 调试日志：打印计算后的原图坐标（确认是否正确）
    //     printf("原图坐标: x1=%.2f, y1=%.2f, x2=%.2f, y2=%.2f\n", 
    //            x1, y1, x2, y2);

        // 边界保护
        x1 = utils_fmaxf(0.0f, utils_fminf(x1, (float)cam_w));
        y1 = utils_fmaxf(0.0f, utils_fminf(y1, (float)cam_h));
        x2 = utils_fmaxf(0.0f, utils_fminf(x2, (float)cam_w));
        y2 = utils_fmaxf(0.0f, utils_fminf(y2, (float)cam_h));

        // 确保宽高为正
        if (x2 <= x1 || y2 <= y1) continue;

        int ix1 = (int)x1;
        int iy1 = (int)y1;
        int iw  = (int)(x2 - x1);
        int ih  = (int)(y2 - y1);

        if (iw < 15 || ih < 15) continue;

        // 绘制框
        bgr_draw_rect(dst_frame, cam_w, cam_h, ix1, iy1, iw, ih, FACE_BOX_COLOR_RED, FACE_BOX_THICKNESS);
    }
}
// ==========================
// 工具接口
// ==========================
void ai_model_mnn_get_ai_size(int* w, int* h)
{
    if (w) *w = g_priv ? g_priv->ai_w : 0;
    if (h) *h = g_priv ? g_priv->ai_h : 0;
}

bool ai_model_mnn_is_ready(void)
{
    return g_priv && g_priv->ultra_face && g_priv->ultra_face->is_ready();
}

// ==========================
// 核心推理接口（新增格式参数，无多余接口）
// ==========================
int ai_model_mnn_infer_image(const uint8_t* image_data, int cam_w, int cam_h,
                            uint8_t* external_bgr_buf,
                            FaceInfo_C* out_faces, int max_faces, int* out_face_num,
                            uint8_t format)
{
    if (!g_priv || !out_faces || !out_face_num || !external_bgr_buf) {
        return MNN_FACE_ERR_INPUT;
    }

    g_priv->external_bgr_buf = external_bgr_buf;

    vector<FaceInfo_MNN> faces;
    // 直接使用传入的格式参数，无多余接口
    int ret = g_priv->ultra_face->detect(
        image_data, 
        cam_w, 
        cam_h, 
        external_bgr_buf, 
        faces, 
        (ImageFormat)format
    );
    if (ret != MNN_FACE_OK) {
        return ret;
    }

    *out_face_num = min((int)faces.size(), max_faces);
    for (int i = 0; i < *out_face_num; i++) {
        out_faces[i].x1 = faces[i].x1;
        out_faces[i].y1 = faces[i].y1;
        out_faces[i].x2 = faces[i].x2;
        out_faces[i].y2 = faces[i].y2;
        out_faces[i].score = faces[i].score;
    }

    return MNN_FACE_OK;
}