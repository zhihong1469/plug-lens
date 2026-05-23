#include "ai_model_mnn.hpp"
#include "ai_model_base.h"
#include "UltraFaceMNN.hpp"
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
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
// 无OpenCV版：批量坐标映射 + 拷贝图像 + 绘制多个人脸框（最终版）
// ==========================
void ai_model_mnn_map_and_draw_faces(FaceInfo_C* faces, int face_num, 
                                     int cam_w, int cam_h,
                                     const uint8_t *src_frame, uint8_t *dst_frame)
{
    if (!faces || face_num <= 0 || !src_frame || !dst_frame || !g_priv) return;

    // 🔥 关键修复：只拷贝1次原始图像（原代码重复拷贝，效率极低）
    memcpy(dst_frame, src_frame, cam_w * cam_h * 3);

    // 遍历所有人脸，批量坐标映射 + 画框
    float sw = (float)cam_w / g_priv->ai_w;
    float sh = (float)cam_h / g_priv->ai_h;

    for (int i = 0; i < face_num; i++) {
        FaceInfo_C* face = &faces[i];
        
        // 1. 坐标等比例映射（批量计算）
        face->x1 *= sw;
        face->y1 *= sh;
        face->x2 *= sw;
        face->y2 *= sh;

        // 2. 纯C画框（在目标图像上画所有人脸）
        int x = (int)face->x1;
        int y = (int)face->y1;
        int w = (int)(face->x2 - face->x1);
        int h = (int)(face->y2 - face->y1);

        bgr_draw_rect(dst_frame, cam_w, cam_h, x, y, w, h, FACE_BOX_COLOR_RED, FACE_BOX_THICKNESS);
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