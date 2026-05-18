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
#define DEFAULT_NUM_THREAD      1     /**< 单核ARM禁用多线程，避免性能损耗 */
#define DEFAULT_SCORE_THRESH    0.65f
#define DEFAULT_IOU_THRESH      0.3f

// ==========================
// 内部人脸结果结构体
// ==========================
/**
 * @brief MNN推理内部人脸结果
 * @note  仅内部使用，最终转换为FaceInfo_C对外输出
 */
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
} FaceInfo_MNN;

/**
 * @brief UltraFace MNN 人脸检测类
 * @note  封装MNN推理、YUYV转换、NMS、后处理全流程
 */
class UltraFaceMNN {
public:
    UltraFaceMNN();
    ~UltraFaceMNN();

    /**
     * @brief 初始化模型
     * @param model_path 模型路径
     * @param ai_w 模型输入宽
     * @param ai_h 模型输入高
     * @param score_threshold 置信度阈值
     * @param iou_threshold NMS阈值
     * @return 错误码
     */
    int init(const char* model_path, 
            int ai_w, 
            int ai_h,
            float score_threshold = DEFAULT_SCORE_THRESH,
            float iou_threshold = DEFAULT_IOU_THRESH);

    /**
     * @brief 人脸检测推理入口
     * @param yuyv_data 原始YUYV数据
     * @param cam_w 摄像头宽
     * @param cam_h 摄像头高
     * @param face_list 输出人脸结果
     * @return 错误码
     */
    int detect(const uint8_t* yuyv_data, int cam_w, int cam_h, std::vector<FaceInfo_MNN>& face_list);

    /**
     * @brief 坐标映射到原始图像
     */
    static void map_face_to_original(FaceInfo_MNN& face, int ai_w, int ai_h, int cam_w, int cam_h);
    
    /**
     * @brief 调试：绘制人脸框
     */
    static void draw_faces(cv::Mat& img, const std::vector<FaceInfo_MNN>& face_list);

    /**
     * @brief 释放资源
     */
    void deinit();
    
    /**
     * @brief 检查初始化状态
     */
    bool is_ready() const { return m_ready; }

private:
    /**
     * @brief YUYV格式转BGR
     */
    int yuyv_to_bgr(const uint8_t* yuyv_data, int width, int height, cv::Mat& out_img);
    
    /**
     * @brief 生成检测框
     */
    void generate_bbox(std::vector<FaceInfo_MNN>& bbox_collection, MNN::Tensor* scores, MNN::Tensor* boxes);
    
    /**
     * @brief 非极大值抑制去重
     */
    void nms(std::vector<FaceInfo_MNN>& input, std::vector<FaceInfo_MNN>& output);

private:
    std::shared_ptr<MNN::Interpreter> m_interpreter;  /**< MNN解释器 */
    MNN::Session* m_session;                           /**< MNN会话 */
    MNN::Tensor* m_input_tensor;                      /**< 模型输入张量 */

    int m_ai_w;           /**< 模型输入宽 */
    int m_ai_h;           /**< 模型输入高 */
    int m_num_thread;     /**< 推理线程数 */
    float m_score_thresh; /**< 置信度阈值 */
    float m_iou_thresh;   /**< NMS阈值 */
    bool m_ready;         /**< 初始化状态 */

    // 图像预处理参数
    const float m_mean_vals[3]  = {127, 127, 127};
    const float m_norm_vals[3] = {1.0f / 128, 1.0f / 128, 1.0f / 128};
    
    // Anchor 相关参数
    const float m_center_variance = 0.1f;
    const float m_size_variance   = 0.2f;
    std::vector<std::vector<float>> m_min_boxes;
    std::vector<float> m_strides;
    std::vector<std::vector<float>> m_priors;
    int m_num_anchors;
};

/** @} */ // end of ultra_face_mnn

#endif // ULTRA_FACE_MNN_HPP