#include "ai_model_mnn.hpp"
#include "ai_model_base.h"
#include "UltraFaceMNN.hpp"
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

// ==========================
// MNN 子类私有数据
// ==========================
typedef struct {
    UltraFaceMNN*    ultra_face;
    int              ai_w;
    int              ai_h;
    uint8_t*         yuyv_frame;
    vector<FaceInfo_MNN> curr_faces;
} mnn_priv_t;

// 全局单例（供旧接口使用）
static ai_model_handle_t* g_mnn_handle = nullptr;
static mnn_priv_t*        g_priv        = nullptr;

// ==========================
// 1. 初始化
// ==========================
static ai_model_err_t mnn_ai_init(ai_model_handle_t* handle)
{
    // ====================== 新增调试日志 ======================
    printf("[AI_INIT_DEBUG] ================== AI初始化开始 ==================\n");
    printf("[AI_INIT_DEBUG] 入参handle=%p\n", handle);

    if (!handle) {
        printf("[AI_INIT_ERROR] handle为空！\n");
        return AI_MODEL_ERR_PARAM;
    }

    // 1. 申请私有数据
    mnn_priv_t* priv = new(nothrow) mnn_priv_t;
    printf("[AI_INIT_DEBUG] new mnn_priv_t = %p\n", priv);
    if (!priv) {
        printf("[AI_INIT_ERROR] 内存不足，创建priv失败！\n");
        return AI_MODEL_ERR_NO_MEM;
    }

    // 2. 创建UltraFace实例
    priv->ultra_face = new(nothrow) UltraFaceMNN();
    printf("[AI_INIT_DEBUG] new UltraFaceMNN = %p\n", priv->ultra_face);
    if (!priv->ultra_face) {
        printf("[AI_INIT_ERROR] 创建UltraFace失败！\n");
        delete priv;
        return AI_MODEL_ERR_NO_MEM;
    }

    // 3. 核心：模型初始化（99%概率死在这里！）
    printf("[AI_INIT_DEBUG] 开始加载模型：path=%s | w=%d | h=%d | score=%.2f | iou=%.2f\n",
           handle->config.model_path,
           handle->config.input_width,
           handle->config.input_height,
           handle->config.score_thresh,
           handle->config.iou_thresh);

    int ret = priv->ultra_face->init(
        handle->config.model_path,
        handle->config.input_width,
        handle->config.input_height,
        handle->config.score_thresh,
        handle->config.iou_thresh
    );
    printf("[AI_INIT_DEBUG] UltraFace init 返回值=%d\n", ret);

    if (ret != MNN_FACE_OK) {
        printf("[AI_INIT_ERROR] 模型初始化失败！请检查模型文件/路径！\n");
        delete priv->ultra_face;
        delete priv;
        return AI_MODEL_ERR_INIT;
    }

    // 4. 初始化成功，赋值全局g_priv（关键！）
    priv->ai_w = handle->config.input_width;
    priv->ai_h = handle->config.input_height;
    handle->user_data = priv;
    
    g_priv = priv;
    g_mnn_handle = handle;
    printf("[AI_INIT_SUCCESS] ================== AI初始化完成！g_priv=%p ==================\n", g_priv);

    return AI_MODEL_OK;
}

// ==========================
// 2. 输入图像
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
// 3. 推理
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
// 4. 获取结果
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
// 5. 反初始化
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
// C-OOP 操作表
// ==========================
static const ai_model_ops_t g_mnn_ai_ops = {
    .init       = mnn_ai_init,
    .input      = mnn_ai_input,
    .infer      = mnn_ai_infer,
    .get_result = mnn_ai_get_result,
    .deinit     = mnn_ai_deinit,
};

// ==========================
// 创建接口
// ==========================
ai_model_handle_t* ai_model_mnn_create(const ai_model_config_t* config)
{
    // ====================== 新增调试日志 ======================
    printf("[AI_CREATE_DEBUG] 模型创建入口！config=%p | model_path=%s\n", 
           config, config ? config->model_path : "NULL");
    
    ai_model_handle_t* handle = ai_model_create(config, &g_mnn_ai_ops);
    
    printf("[AI_CREATE_DEBUG] 模型创建完成！返回handle=%p\n", handle);
    return handle;
}

// ==========================
// 实用C接口（完整保留）
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
    // ====================== 调试日志：入参检查 ======================
    printf("[AI_MNN_DEBUG] 推理入口 | 数据指针=%p | 摄像头宽=%d | 高=%d | 输出数组=%p\n",
           yuyv_data, cam_w, cam_h, out_faces);
    printf("[AI_MNN_DEBUG] 全局私有数据=%p | 模型实例=%p\n",
           g_priv, g_priv ? g_priv->ultra_face : NULL);

    // 原参数检查
    if (!g_priv || !out_faces || !out_face_num) {
        printf("[AI_MNN_ERROR] 入参非法！返回 MNN_FACE_ERR_INPUT\n");
        return MNN_FACE_ERR_INPUT;
    }

    vector<FaceInfo_MNN> faces;
    // ====================== 调试日志：调用核心detect ======================
    int ret = g_priv->ultra_face->detect(yuyv_data, cam_w, cam_h, faces);
    printf("[AI_MNN_DEBUG] detect() 执行完成，返回码=%d\n", ret);

    // 原错误返回
    if (ret != MNN_FACE_OK) {
        printf("[AI_MNN_ERROR] 核心detect推理失败！错误码=%d\n", ret);
        return ret;
    }

    // ====================== 调试日志：结果数量 ======================
    *out_face_num = min((int)faces.size(), max_faces);
    printf("[AI_MNN_DEBUG] 检测到人脸数量=%d | 实际输出=%d\n",
           (int)faces.size(), *out_face_num);

    for (int i = 0; i < *out_face_num; i++) {
        out_faces[i].x1 = faces[i].x1;
        out_faces[i].y1 = faces[i].y1;
        out_faces[i].x2 = faces[i].x2;
        out_faces[i].y2 = faces[i].y2;
        out_faces[i].score = faces[i].score;
    }

    printf("[AI_MNN_DEBUG] 推理全部成功！\n");
    return MNN_FACE_OK;
}