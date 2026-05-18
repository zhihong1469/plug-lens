#ifndef __AI_MODEL_MNN_HPP
#define __AI_MODEL_MNN_HPP

#include "ai_model_base.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @defgroup ai_model_mnn MNN人脸检测模型（纯C接口）
 * @brief 基于MNN的UltraFace人脸检测，上层C服务专用接口
 * @note  适配FrameLink零拷贝、YUYV原始图像、IMX6ULL嵌入式平台
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

// ==========================
// MNN人脸检测 错误码
// ==========================
#define MNN_FACE_OK             0    /**< 推理/操作成功 */
#define MNN_FACE_ERR_INIT       -1   /**< 模型初始化失败 */
#define MNN_FACE_ERR_MODEL      -2   /**< 模型文件加载失败 */
#define MNN_FACE_ERR_INPUT      -3   /**< 输入参数非法（空指针/分辨率错误）*/
#define MNN_FACE_ERR_INFER      -4   /**< 模型推理执行失败 */

// ==========================
// 模型最优默认配置（IMX6ULL）
// ==========================
#define DEFAULT_AI_W            320    /**< 模型输入宽度（训练分辨率）*/
#define DEFAULT_AI_H            240    /**< 模型输入高度（训练分辨率）*/
#define DEFAULT_SCORE_THRESH    0.65f  /**< 人脸置信度阈值 */
#define DEFAULT_IOU_THRESH      0.3f   /**< NMS阈值 */

// ==========================
// 上层C服务 人脸结果结构体
// ==========================
/**
 * @brief 纯C人脸检测结果
 * @note  上层业务唯一使用的人脸格式，坐标为模型输出相对坐标
 */
typedef struct {
    float x1;      /**< 左上角X坐标 */
    float y1;      /**< 左上角Y坐标 */
    float x2;      /**< 右下角X坐标 */
    float y2;      /**< 右下角Y坐标 */
    float score;   /**< 置信度 0~1 */
} FaceInfo_C;

// ==========================
// C-OOP 核心创建接口
// ==========================
/**
 * @brief 创建MNN人脸检测模型句柄
 * @param config 模型配置（路径/尺寸/阈值）
 * @return 成功返回模型句柄，失败返回NULL
 * @note  全局单例，服务初始化调用一次
 */
ai_model_handle_t* ai_model_mnn_create(const ai_model_config_t* config);

// ==========================
// 上层服务 实用工具接口
// ==========================
/**
 * @brief 人脸坐标映射：模型输出 → 摄像头原始分辨率
 * @param face 人脸结果
 * @param cam_w 摄像头宽度（如640）
 * @param cam_h 摄像头高度（如360）
 * @note  推理后必须调用，匹配原始图像坐标
 */
void ai_model_mnn_map_face(FaceInfo_C* face, int cam_w, int cam_h);

/**
 * @brief 获取模型输入分辨率
 * @param[out] w 模型宽度
 * @param[out] h 模型高度
 */
void ai_model_mnn_get_ai_size(int* w, int* h);

/**
 * @brief 检查模型是否就绪
 * @return true=可推理，false=未初始化
 */
bool ai_model_mnn_is_ready(void);

// ==========================
// 核心推理接口（FrameLink专用）
// ==========================
/**
 * @brief YUYV原始图像 人脸检测推理（零拷贝）
 * @param yuyv_data 摄像头YUYV数据指针
 * @param cam_w 摄像头宽度
 * @param cam_h 摄像头高度
 * @param out_faces 输出人脸结果数组
 * @param max_faces 最大检测人脸数
 * @param out_face_num 实际检测人脸数
 * @return 成功返回MNN_FACE_OK
 * @note  1. 内部自动转换YUYV→BGR+缩放
 *        2. 直接使用FrameLink零拷贝数据，性能最优
 *        3. 嵌入式专用高性能接口
 */
int ai_model_mnn_infer_yuyv(const uint8_t* yuyv_data, int cam_w, int cam_h,
                            FaceInfo_C* out_faces, int max_faces, int* out_face_num);

#ifdef __cplusplus
}
#endif

/** @} */ // end of ai_model_mnn

#endif // __AI_MODEL_MNN_HPP