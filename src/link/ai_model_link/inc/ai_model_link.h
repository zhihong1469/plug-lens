#ifndef AI_MODEL_LINK_H
#define AI_MODEL_LINK_H

#include <stdint.h>
#include <stdbool.h>

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

// 纯C人脸结构体
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
} FaceInfo_C;

// 【修复】统一函数名为 ai_model_link_*
int ai_model_link_init(const char* model_path, 
                       int ai_w, 
                       int ai_h,
                       float score_threshold,
                       float iou_threshold);

int ai_model_link_infer(const uint8_t* yuyv_data, int cam_w, int cam_h,
                       FaceInfo_C* out_faces, int max_faces, int* out_face_num);

void ai_model_link_map_face(FaceInfo_C* face, int ai_w, int ai_h, int cam_w, int cam_h);
void ai_model_link_get_ai_size(int* w, int* h);
bool ai_model_link_is_ready(void);
void ai_model_link_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // AI_MODEL_LINK_H