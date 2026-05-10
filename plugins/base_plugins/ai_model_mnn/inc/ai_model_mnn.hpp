#ifndef __AI_MODEL_MNN_HPP
#define __AI_MODEL_MNN_HPP

#include "ai_model_base.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================
// 【从 ai_model_link 完整迁移】
// 错误码、宏、纯C结构体 全部放在这里
// ==========================
#ifdef __cplusplus
extern "C" {
#endif

// 错误码定义
#define MNN_FACE_OK             0
#define MNN_FACE_ERR_INIT       -1
#define MNN_FACE_ERR_MODEL      -2
#define MNN_FACE_ERR_INPUT      -3
#define MNN_FACE_ERR_INFER      -4

// AI模型默认配置
#define DEFAULT_AI_W            320
#define DEFAULT_AI_H            240
#define DEFAULT_SCORE_THRESH    0.65f
#define DEFAULT_IOU_THRESH      0.3f

// 纯C人脸结构体（上层C代码唯一识别的结果格式）
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
} FaceInfo_C;

// ==========================
// C-OOP 核心创建接口
// ==========================
ai_model_handle_t* ai_model_mnn_create(const ai_model_config_t* config);

// ==========================
// 【保留所有实用C接口】上层C代码无缝调用
// ==========================
void  ai_model_mnn_map_face(FaceInfo_C* face, int cam_w, int cam_h);
void  ai_model_mnn_get_ai_size(int* w, int* h);
bool  ai_model_mnn_is_ready(void);
int   ai_model_mnn_infer_yuyv(const uint8_t* yuyv_data, int cam_w, int cam_h,
                              FaceInfo_C* out_faces, int max_faces, int* out_face_num);

#ifdef __cplusplus
}
#endif

#endif // __AI_MODEL_MNN_HPP