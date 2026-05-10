#include "ai_model_link.h"
#include "ai_model_mnn.hpp"

// 全局单例AI对象
static UltraFaceMNN g_ultra_face;
// 保存实际初始化的AI分辨率
static int g_ai_w = DEFAULT_AI_W;
static int g_ai_h = DEFAULT_AI_H;

#ifdef __cplusplus
extern "C" {
#endif

int ai_model_link_init(const char* model_path, int ai_w, int ai_h,
                       float score_threshold, float iou_threshold) {
    g_ai_w = ai_w;
    g_ai_h = ai_h;
    return g_ultra_face.init(model_path, ai_w, ai_h, score_threshold, iou_threshold);
}

int ai_model_link_infer(const uint8_t* yuyv_data, int cam_w, int cam_h,
                       FaceInfo_C* out_faces, int max_faces, int* out_face_num) {
    if (!out_faces || !out_face_num || max_faces <= 0) return MNN_FACE_ERR_INPUT;

    // 【修复】使用 FaceInfo_MNN
    std::vector<FaceInfo_MNN> faces;
    int ret = g_ultra_face.detect(yuyv_data, cam_w, cam_h, faces);
    if (ret != MNN_FACE_OK) return ret;

    *out_face_num = std::min((int)faces.size(), max_faces);
    for (int i = 0; i < *out_face_num; i++) {
        out_faces[i].x1 = faces[i].x1;
        out_faces[i].y1 = faces[i].y1;
        out_faces[i].x2 = faces[i].x2;
        out_faces[i].y2 = faces[i].y2;
        out_faces[i].score = faces[i].score;
    }
    return MNN_FACE_OK;
}

void ai_model_link_map_face(FaceInfo_C* face, int ai_w, int ai_h, int cam_w, int cam_h) {
    if (!face) return;
    float sw = (float)cam_w / ai_w;
    float sh = (float)cam_h / ai_h;
    face->x1 *= sw;
    face->y1 *= sh;
    face->x2 *= sw;
    face->y2 *= sh;
}

void ai_model_link_get_ai_size(int* w, int* h) {
    if (w) *w = g_ai_w;
    if (h) *h = g_ai_h;
}

bool ai_model_link_is_ready(void) {
    return g_ultra_face.is_ready();
}

void ai_model_link_deinit(void) {
    g_ultra_face.deinit();
}

#ifdef __cplusplus
}
#endif