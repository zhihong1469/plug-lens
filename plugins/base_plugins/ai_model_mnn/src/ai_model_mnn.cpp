#include "ai_model_mnn.hpp"
#include "ai_model_base.h"
#include "UltraFaceMNN.hpp"
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

// ==========================
// MNN 子类私有数据（C++ 内部使用）
// ==========================
typedef struct {
    UltraFaceMNN*    ultra_face;
    int              ai_w;
    int              ai_h;
    uint8_t*         yuyv_frame;
    int              cam_w;
    int              cam_h;
    vector<FaceInfo_MNN> curr_faces;
} mnn_priv_t;

// 全局单例句柄（兼容你旧框架，上层无感知）
static ai_model_handle_t* g_mnn_handle = NULL;
static mnn_priv_t*       g_priv       = NULL;

// ==========================
// 1. 初始化：对接基类接口
// ==========================
static ai_model_err_t mnn_ai_init(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    // 分配私有数据
    mnn_priv_t* priv = new(nothrow) mnn_priv_t;
    if (!priv) return AI_MODEL_ERR_NO_MEM;

    // 创建MNN推理实例
    priv->ultra_face = new(nothrow) UltraFaceMNN();
    if (!priv->ultra_face) {
        delete priv;
        return AI_MODEL_ERR_NO_MEM;
    }

    // 调用你原有MNN初始化
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

    // 保存AI尺寸
    priv->ai_w = handle->config.input_width;
    priv->ai_h = handle->config.input_height;

    // 绑定到基类句柄
    handle->user_data = priv;
    g_priv = priv;
    g_mnn_handle = handle;

    return AI_MODEL_OK;
}

// ==========================
// 2. 输入图像：保存YUYV帧信息
// ==========================
static ai_model_err_t mnn_ai_input(ai_model_handle_t* handle,
                                   uint8_t* data, uint32_t len)
{
    if (!handle || !data) return AI_MODEL_ERR_PARAM;
    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_ERR_INIT;

    priv->yuyv_frame = data;
    return AI_MODEL_OK;
}

// ==========================
// 3. 执行推理：基类接口
// ==========================
static ai_model_err_t mnn_ai_infer(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;
    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv || !priv->yuyv_frame) return AI_MODEL_ERR_INIT;

    priv->curr_faces.clear();
    int ret = priv->ultra_face->detect(
        priv->yuyv_frame,
        priv->ai_w,
        priv->ai_h,
        priv->curr_faces
    );

    return (ret == MNN_FACE_OK) ? AI_MODEL_OK : AI_MODEL_ERR_INFER;
}

// ==========================
// 4. 获取结果：基类接口
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
// 5. 反初始化：基类接口
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
    handle->user_data = NULL;
    g_priv = NULL;
    g_mnn_handle = NULL;

    return AI_MODEL_OK;
}

// ==========================
// C-OOP 操作表（固定不变）
// ==========================
static const ai_model_ops_t g_mnn_ai_ops = {
    .init       = mnn_ai_init,
    .input      = mnn_ai_input,
    .infer      = mnn_ai_infer,
    .get_result = mnn_ai_get_result,
    .deinit     = mnn_ai_deinit,
};

// ==========================
// C-OOP 创建接口（对外）
// ==========================
ai_model_handle_t* ai_model_mnn_create(const ai_model_config_t* config)
{
    return ai_model_create(config, &g_mnn_ai_ops);
}

// ==========================
// 上层C代码直接调用，零修改
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

void ai_model_mnn_get_ai_size(int* w, int* h)
{
    if (w) *w = g_priv ? g_priv->ai_w : 0;
    if (h) *h = g_priv ? g_priv->ai_h : 0;
}

bool ai_model_mnn_is_ready(void)
{
    return g_priv && g_priv->ultra_face && g_priv->ultra_face->is_ready();
}

int ai_model_mnn_infer_yuyv(const uint8_t* yuyv_data, int cam_w, int cam_h,
                            FaceInfo_C* out_faces, int max_faces, int* out_face_num)
{
    if (!g_priv || !out_faces || !out_face_num) return MNN_FACE_ERR_INPUT;

    // 直接调用你原有MNN推理逻辑
    vector<FaceInfo_MNN> faces;
    int ret = g_priv->ultra_face->detect(yuyv_data, cam_w, cam_h, faces);
    if (ret != MNN_FACE_OK) return ret;

    // C++ vector → C 结构体转换
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