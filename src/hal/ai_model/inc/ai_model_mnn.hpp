#ifndef AI_MODEL_MNN_HPP
#define AI_MODEL_MNN_HPP

#include <memory>
#include <vector>
#include <stdint.h>
#include "Interpreter.hpp"
#include "Tensor.hpp"
#include "ImageProcess.hpp"
#include <opencv2/opencv.hpp>

// 错误码定义
#define MNN_FACE_OK             0
#define MNN_FACE_ERR_INIT       -1
#define MNN_FACE_ERR_MODEL      -2
#define MNN_FACE_ERR_INPUT      -3
#define MNN_FACE_ERR_INFER      -4

// AI模型默认配置 (IMX6ULL最优)
#define DEFAULT_AI_W            320
#define DEFAULT_AI_H            240
#define DEFAULT_NUM_THREAD      1
#define DEFAULT_SCORE_THRESH    0.65f
#define DEFAULT_IOU_THRESH      0.3f

// 【修复】统一结构体名称
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
} FaceInfo_MNN; 

class UltraFaceMNN {
public:
    UltraFaceMNN();
    ~UltraFaceMNN();

    int init(const char* model_path, 
            int ai_w, 
            int ai_h,
            float score_threshold = DEFAULT_SCORE_THRESH,
            float iou_threshold = DEFAULT_IOU_THRESH);

    // 【修复】参数使用 FaceInfo_MNN
    int detect(const uint8_t* yuyv_data, int cam_w, int cam_h, std::vector<FaceInfo_MNN>& face_list);

    static void map_face_to_original(FaceInfo_MNN& face, int ai_w, int ai_h, int cam_w, int cam_h);
    static void draw_faces(cv::Mat& img, const std::vector<FaceInfo_MNN>& face_list);

    void deinit();
    bool is_ready() const { return m_ready; }

private:
    int yuyv_to_bgr(const uint8_t* yuyv_data, int width, int height, cv::Mat& out_img);
    void generate_bbox(std::vector<FaceInfo_MNN>& bbox_collection, MNN::Tensor* scores, MNN::Tensor* boxes);
    void nms(std::vector<FaceInfo_MNN>& input, std::vector<FaceInfo_MNN>& output);

private:
    std::shared_ptr<MNN::Interpreter> m_interpreter;
    MNN::Session* m_session;
    MNN::Tensor* m_input_tensor;

    int m_ai_w;
    int m_ai_h;
    int m_num_thread;
    float m_score_thresh;
    float m_iou_thresh;
    bool m_ready;

    const float m_mean_vals[3]  = {127, 127, 127};
    const float m_norm_vals[3] = {1.0f / 128, 1.0f / 128, 1.0f / 128};
    const float m_center_variance = 0.1f;
    const float m_size_variance   = 0.2f;

    std::vector<std::vector<float>> m_min_boxes;
    std::vector<float> m_strides;
    std::vector<std::vector<float>> m_priors;
    int m_num_anchors;
};

#endif // AI_MODEL_MNN_HPP