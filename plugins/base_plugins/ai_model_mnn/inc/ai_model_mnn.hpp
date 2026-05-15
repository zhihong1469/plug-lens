#ifndef __AI_MODEL_MNN_HPP
#define __AI_MODEL_MNN_HPP

#include "ai_model_base.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief MNN人脸检测模型 纯C接口头文件
 * @note  上层C语言服务唯一可用接口，禁止访问内部私有实现
 *        所有接口专为嵌入式Linux Vision AI框架设计
 */

#ifdef __cplusplus
extern "C" {
#endif

// ==========================
// 错误码定义
// ==========================
/** 推理/操作成功 */
#define MNN_FACE_OK             0
/** 模型初始化失败 */
#define MNN_FACE_ERR_INIT       -1
/** 模型文件加载失败 */
#define MNN_FACE_ERR_MODEL      -2
/** 输入参数非法（空指针/分辨率错误） */
#define MNN_FACE_ERR_INPUT      -3
/** 模型推理执行失败 */
#define MNN_FACE_ERR_INFER      -4

// ==========================
// AI模型默认配置（训练最优参数）
// ==========================
/** 模型输入最优宽度（训练分辨率） */
#define DEFAULT_AI_W            320
/** 模型输入最优高度（训练分辨率） */
#define DEFAULT_AI_H            240
/** 人脸检测置信度阈值 */
#define DEFAULT_SCORE_THRESH    0.65f
/** 人脸检测NMS非极大值抑制阈值 */
#define DEFAULT_IOU_THRESH      0.3f

// ==========================
// 纯C人脸结果结构体
/* @brief 上层C服务唯一识别的人脸检测结果格式
 * @note  坐标为模型输出的相对坐标，需映射后使用
 */
typedef struct {
    float x1;      /**< 人脸左上角X坐标 */
    float y1;      /**< 人脸左上角Y坐标 */
    float x2;      /**< 人脸右下角X坐标 */
    float y2;      /**< 人脸右下角Y坐标 */
    float score;   /**< 人脸置信度分数 0~1 */
} FaceInfo_C;

// ==========================
// C-OOP 核心创建接口
// ==========================
/**
 * @brief 创建MNN人脸检测模型句柄
 * @param config 模型配置参数（路径/输入尺寸/阈值）
 * @return 成功返回模型句柄，失败返回NULL
 * @note  服务初始化时调用一次，全局单例使用
 */
ai_model_handle_t* ai_model_mnn_create(const ai_model_config_t* config);

// ==========================
// 上层C服务 实用工具接口
// ==========================
/**
 * @brief 人脸坐标映射：将模型输出坐标 → 摄像头原始分辨率坐标
 * @param face 待映射的人脸结果
 * @param cam_w 摄像头原始宽度（如640）
 * @param cam_h 摄像头原始高度（如360）
 * @note  必须在推理后调用，否则坐标不匹配屏幕/原始图像
 */
void  ai_model_mnn_map_face(FaceInfo_C* face, int cam_w, int cam_h);

/**
 * @brief 获取模型内部训练分辨率
 * @param[out] w 模型输入宽度
 * @param[out] h 模型输入高度
 * @note  用于调试/校验，无需用于推理接口
 */
void  ai_model_mnn_get_ai_size(int* w, int* h);

/**
 * @brief 检查模型是否初始化完成
 * @return true=就绪可推理，false=未初始化
 */
bool  ai_model_mnn_is_ready(void);

/**
 * @brief YUYV格式图像 人脸检测推理（核心业务接口）
 * @param yuyv_data 摄像头原始YUYV格式数据指针
 * @param cam_w     摄像头原始宽度（如640）
 * @param cam_h     摄像头原始高度（如360）
 * @param out_faces 输出人脸结果数组
 * @param max_faces 最大支持检测的人脸数量
 * @param out_face_num 输出实际检测到的人脸数量
 * @return 成功返回MNN_FACE_OK，失败返回错误码
 * @note  1. 无需外部做分辨率/格式转换，模型内部自动处理640x360→320x240
 *        2. 直接传入摄像头原始数据，性能最优
 *        3. 为FrameLink帧数据专用推理接口
 */
int   ai_model_mnn_infer_yuyv(const uint8_t* yuyv_data, int cam_w, int cam_h,
                              FaceInfo_C* out_faces, int max_faces, int* out_face_num);

#ifdef __cplusplus
}
#endif

#endif // __AI_MODEL_MNN_HPP