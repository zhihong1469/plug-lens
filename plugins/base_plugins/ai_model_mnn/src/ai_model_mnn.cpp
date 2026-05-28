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

/**
 * @brief  人脸坐标映射+多色框绘制 + 静止去重存储判断(适配等比例)
 * @param  faces: 模型输出人脸结果
 * @param  face_num: 检测到的人脸数量
 * @param  cam_w: 摄像头原始宽度
 * @param  cam_h: 摄像头原始高度
 * @param  src_frame: 原始图像数据
 * @param  dst_frame: 绘制完成后的图像数据
 * @return  int: 1=需要存储图片，0=人脸静止无需存储
 * @details 功能特性：
 *          1. 所有有效人脸全部绘制，无遗漏
 *          2. 三色区分：移动=红色、静止=蓝色、低置信度=绿色
 *          3. 任意人脸移动即触发存图，存图时绘制所有人脸
 *          4. 无人移动时不绘制、不存图，零额外开销
 */
int ai_model_mnn_map_and_draw_faces(FaceInfo_C* faces, int face_num, 
                                     int cam_w, int cam_h,
                                     const uint8_t *src_frame, uint8_t *dst_frame)
{
    if (!faces || face_num <= 0 || !src_frame || !dst_frame || !g_priv) {
        return 0; // 异常情况不存图
    }

    // 静态缓存：保存上一帧有效人脸数据（用于静止判断）
    static FaceInfo_C last_valid_faces[FACE_DETECT_MAX_FACES] = {0};
    static int last_face_count = 0;
    static bool is_first_frame = true;

    // 当前帧有效人脸缓存 + 移动状态标记
    FaceInfo_C curr_valid_faces[FACE_DETECT_MAX_FACES] = {0};
    bool face_moved[FACE_DETECT_MAX_FACES] = {false}; // 标记每个人脸是否移动
    int curr_face_count = 0;
    int need_save = 0; // 默认不存图，检测到移动再置1

    // 拷贝原图（无论是否存图都拷贝，保证dst_frame始终有有效数据）
    memcpy(dst_frame, src_frame, cam_w * cam_h * 3);

    int model_w = g_priv->ai_w;  // 160
    int model_h = g_priv->ai_h;  // 120

    // 等比例缩放计算（与rgb_resize完全一致，无修改）
    float scale_w = (float)model_w / cam_w;
    float scale_h = (float)model_h / cam_h;
    float scale    = utils_fmaxf(scale_w, scale_h);

    float scaled_w = cam_w * scale;
    float scaled_h = cam_h * scale;
    float crop_x   = (scaled_w - model_w) / 2.0f;
    float crop_y   = (scaled_h - model_h) / 2.0f;

    const float score_thresh = 0.5f;

    // ===================== 第一步：筛选当前帧有效人脸【无过滤，全员保留】 =====================
    for (int i = 0; i < face_num && curr_face_count < FACE_DETECT_MAX_FACES; i++) 
    {
        FaceInfo_C* face = &faces[i];
        if (face->score < score_thresh) continue; // 仅过滤极低置信度无效人脸

        // 模型坐标转换为原图坐标
        float x1 = (face->x1 + crop_x) / scale;
        float y1 = (face->y1 + crop_y) / scale;
        float x2 = (face->x2 + crop_x) / scale;
        float y2 = (face->y2 + crop_y) / scale;

        // 边界保护
        x1 = utils_fmaxf(0.0f, utils_fminf(x1, (float)cam_w));
        y1 = utils_fmaxf(0.0f, utils_fminf(y1, (float)cam_h));
        x2 = utils_fmaxf(0.0f, utils_fminf(x2, (float)cam_w));
        y2 = utils_fmaxf(0.0f, utils_fminf(y2, (float)cam_h));

        // 仅过滤完全无效的框
        if (x2 <= x1 || y2 <= y1) continue;

        // 保存所有有效人脸（无任何额外过滤）
        curr_valid_faces[curr_face_count].x1 = x1;
        curr_valid_faces[curr_face_count].y1 = y1;
        curr_valid_faces[curr_face_count].x2 = x2;
        curr_valid_faces[curr_face_count].y2 = y2;
        curr_valid_faces[curr_face_count].score = face->score;
        curr_face_count++;
    }

    // ===================== 第二步：核心 - 逐人脸移动判断【标记每个人脸状态】 =====================
    if (is_first_frame) {
        // 第一帧强制存图，所有人脸标记为移动（显示红色）
        is_first_frame = false;
        need_save = 1;
        for (int i = 0; i < curr_face_count; i++) {
            face_moved[i] = true;
        }
    } else {
        // 规则1：人脸数量变化 → 必须存图
        if (curr_face_count != last_face_count) {
            need_save = 1;
            // 数量变化时，所有人脸标记为移动
            for (int i = 0; i < curr_face_count; i++) {
                face_moved[i] = true;
            }
        }
        // 规则2：数量相同，逐人脸匹配判断是否移动
        else if (curr_face_count > 0) {
            for (int i = 0; i < curr_face_count; i++) {
                face_moved[i] = true; // 默认标记为移动
                // 计算当前人脸中心坐标
                float curr_cx = (curr_valid_faces[i].x1 + curr_valid_faces[i].x2) / 2.0f;
                float curr_cy = (curr_valid_faces[i].y1 + curr_valid_faces[i].y2) / 2.0f;

                // 遍历上一帧所有人脸，找最近匹配（解决乱序问题）
                for (int j = 0; j < last_face_count; j++) {
                    float last_cx = (last_valid_faces[j].x1 + last_valid_faces[j].x2) / 2.0f;
                    float last_cy = (last_valid_faces[j].y1 + last_valid_faces[j].y2) / 2.0f;

                    float dx = utils_fabsf(curr_cx - last_cx);
                    float dy = utils_fabsf(curr_cy - last_cy);
                    // 中心距离小于阈值 → 是同一个人，且未移动
                    if (dx < FACE_STATIC_THRESHOLD && dy < FACE_STATIC_THRESHOLD) {
                        face_moved[i] = false;
                        break;
                    }
                }

                // 任意一张人脸移动 → 触发存图
                if (face_moved[i]) {
                    need_save = 1;
                }
            }
        }
        // 规则3：无人脸 → 不存图
        else {
            need_save = 0;
        }
    }

    // ===================== 第三步：更新上一帧缓存 =====================
    last_face_count = curr_face_count;
    for (int i = 0; i < curr_face_count && i < FACE_DETECT_MAX_FACES; i++) {
        last_valid_faces[i] = curr_valid_faces[i];
    }

    // ===================== 第四步：多色绘制人脸框【仅存图时才绘制】 =====================
    if (need_save) {
        for (int i = 0; i < curr_face_count; i++)
        {
            int ix1 = (int)curr_valid_faces[i].x1;
            int iy1 = (int)curr_valid_faces[i].y1;
            int iw  = (int)(curr_valid_faces[i].x2 - curr_valid_faces[i].x1);
            int ih  = (int)(curr_valid_faces[i].y2 - curr_valid_faces[i].y1);

            // 仅过滤极小无效框（5像素以下）
            if (iw < 5 || ih < 5) continue;

            // 根据状态选择框颜色
            uint32_t box_color;
            if (curr_valid_faces[i].score < FACE_LOW_SCORE_THRESH) {
                box_color = FACE_BOX_COLOR_GREEN;  // 低置信度：绿色
            } else if (face_moved[i]) {
                box_color = FACE_BOX_COLOR_RED;    // 移动人脸：红色
            } else {
                box_color = FACE_BOX_COLOR_BLUE;   // 静止人脸：蓝色
            }

            // 绘制人脸框（所有有效人脸全部绘制）
            bgr_draw_rect(dst_frame, cam_w, cam_h, ix1, iy1, iw, ih, box_color, FACE_BOX_THICKNESS);
        }
    }

    // 返回存储标志：1=存图，0=不存图
    return need_save;
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