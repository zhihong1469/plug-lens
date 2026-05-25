#ifndef ULTRA_FACE_MNN_HPP
#define ULTRA_FACE_MNN_HPP

#include <memory>
#include <vector>
#include <stdint.h>
#include "Interpreter.hpp"
#include "Tensor.hpp"
#include "ImageProcess.hpp"

/**
 * @defgroup ultra_face_mnn MNN UltraFace 人脸检测模块
 * @brief UltraFace人脸检测算法MNN实现，无OpenCV依赖
 * @note 适配IMX6ULL嵌入式平台，支持YUYV/MJPEG双输入格式
 * @{
 */

// ==========================
// 错误码定义（项目通用对齐）
// ==========================
#define MNN_FACE_OK             0
#define MNN_FACE_ERR_INIT       -1
#define MNN_FACE_ERR_MODEL      -2
#define MNN_FACE_ERR_INPUT      -3
#define MNN_FACE_ERR_INFER      -4
#define MNN_FACE_ERR_JPEG       -5  // JPEG解码错误

// ==========================
// 输入图像格式枚举
// ==========================
typedef enum {
    IMAGE_FORMAT_YUYV   = 0,    // YUYV格式（摄像头原始输出）
    IMAGE_FORMAT_MJPEG  = 1     // MJPEG压缩格式
} ImageFormat;

// ==========================
// 嵌入式平台默认配置
// ==========================
#define DEFAULT_AI_W            320
#define DEFAULT_AI_H            240
#define DEFAULT_NUM_THREAD      1
#define DEFAULT_SCORE_THRESH    0.65f
#define DEFAULT_IOU_THRESH      0.3f

// ==========================
// 人脸检测结果结构体
// ==========================
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
} FaceInfo_MNN;

/**
 * @brief UltraFace MNN 人脸检测核心类
 * @details 基于libyuv+MNN实现，无OpenCV依赖，适配嵌入式平台
 */
class UltraFaceMNN {
public:
    UltraFaceMNN();
    ~UltraFaceMNN();

    /**
     * @brief 初始化模型与参数
     * @param model_path MNN模型文件路径
     * @param ai_w 模型输入宽度
     * @param ai_h 模型输入高度
     * @param score_threshold 置信度阈值
     * @param iou_threshold NMS非极大值抑制阈值
     * @return 0成功/负数失败
     */
    int init(const char* model_path, 
            int ai_w, 
            int ai_h,
            float score_threshold = DEFAULT_SCORE_THRESH,
            float iou_threshold = DEFAULT_IOU_THRESH);

    /**
     * @brief 人脸检测推理接口
     * @param input_data 原始图像数据(YUYV/MJPEG)
     * @param cam_w 原始图像宽度
     * @param cam_h 原始图像高度
     * @param external_rgb_buf 外部RGB缓冲区(格式转换专用)
     * @param face_list 人脸检测结果输出
     * @param format 输入图像格式
     * @return 0成功/负数失败
     */
    int detect(const uint8_t* input_data, 
               int cam_w, 
               int cam_h,
               uint8_t* external_rgb_buf,
               std::vector<FaceInfo_MNN>& face_list,
               ImageFormat format = IMAGE_FORMAT_YUYV);

    /**
     * @brief 将AI输出坐标映射回原始图像坐标
     */
    static void map_face_to_original(FaceInfo_MNN& face, int ai_w, int ai_h, int cam_w, int cam_h);

    /**
     * @brief 反初始化，释放资源
     */
    void deinit();
    
    /**
     * @brief 获取模块初始化状态
     */
    bool is_ready() const { return m_ready; }

private:
    // 生成候选框
    void generate_bbox(std::vector<FaceInfo_MNN>& bbox_collection, MNN::Tensor* scores, MNN::Tensor* boxes);
    // 非极大值抑制
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

    // 图像预处理参数
    const float m_mean_vals[3]  = {127, 127, 127};
    const float m_norm_vals[3] = {1.0f / 128, 1.0f / 128, 1.0f / 128};
    
    // 模型锚框参数
    const float m_center_variance = 0.1f;
    const float m_size_variance   = 0.2f;
    std::vector<std::vector<float>> m_min_boxes;
    std::vector<float> m_strides;
    std::vector<std::vector<float>> m_priors;
    int m_num_anchors;
};

/** @} */

#endif // ULTRA_FACE_MNN_HPP