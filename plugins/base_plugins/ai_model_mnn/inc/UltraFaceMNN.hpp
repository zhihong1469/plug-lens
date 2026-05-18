#ifndef ULTRA_FACE_MNN_HPP
#define ULTRA_FACE_MNN_HPP

#include <memory>
#include <vector>
#include <stdint.h>
#include "Interpreter.hpp"
#include "Tensor.hpp"
#include "ImageProcess.hpp"
#include <opencv2/opencv.hpp>

/**
 * @defgroup ultra_face_mnn MNN UltraFace 内部实现
 * @brief UltraFace人脸检测算法MNN实现，C++封装
 * @note  内部私有实现，对外不可见
 * @{
 */

// ==========================
// 内部错误码（与对外接口统一）
// ==========================
#define MNN_FACE_OK             0
#define MNN_FACE_ERR_INIT       -1
#define MNN_FACE_ERR_MODEL      -2
#define MNN_FACE_ERR_INPUT      -3
#define MNN_FACE_ERR_INFER      -4

// ==========================
// 内部配置（IMX6ULL 性能最优）
// ==========================
#define DEFAULT_AI_W            320
#define DEFAULT_AI_H            240
#define DEFAULT_NUM_THREAD      1
#define DEFAULT_SCORE_THRESH    0.65f
#define DEFAULT_IOU_THRESH      0.3f

// ==========================
// 内部人脸结果结构体
// ==========================
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
} FaceInfo_MNN;

/**
 * @brief UltraFace MNN 人脸检测类
 */
class UltraFaceMNN {
public:
    UltraFaceMNN();
    ~UltraFaceMNN();

    int init(const char* model_path, 
            int ai_w, 
            int ai_h,
            float score_threshold = DEFAULT_SCORE_THRESH,
            float iou_threshold = DEFAULT_IOU_THRESH);

    /**
     * @brief 人脸检测推理入口（修改版：支持外部缓存）
     * @param external_bgr_buf 外部传入的BGR缓冲区（AI专属链路内存）
     */
    int detect(const uint8_t* yuyv_data, 
               int cam_w, 
               int cam_h,
               uint8_t* external_bgr_buf,  // 🔥 核心新增：外部专属缓存入口
               std::vector<FaceInfo_MNN>& face_list);

    static void map_face_to_original(FaceInfo_MNN& face, int ai_w, int ai_h, int cam_w, int cam_h);
    static void draw_faces(cv::Mat& img, const std::vector<FaceInfo_MNN>& face_list);

    void deinit();
    bool is_ready() const { return m_ready; }

private:
    /**
     * @brief YUYV转BGR（修改版：直接写入外部缓冲区）
     */
    int yuyv_to_bgr(const uint8_t* yuyv_data, 
                    int width, 
                    int height,
                    uint8_t* bgr_buf);  // 🔥 外部缓存

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

/** @} */

#endif // ULTRA_FACE_MNN_HPP