#ifndef __AI_MODEL_MNN_HPP
#define __AI_MODEL_MNN_HPP

#include "ai_model_base.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * ==================================================================================
 * @file        ai_model_mnn.hpp
 * @brief       MNN UltraFace 人脸检测算法 - 纯C对外接口头文件
 * @details     1. 基于IMX6ULL嵌入式平台优化，适配Linux/RTOS
 *              2. 支持外部BGR内存缓存自动管理（物理隔离主摄像头链路，无内存踩踏）
 *              3. 直接对接FrameLink数据链路，零拷贝读取YUYV原始图像
 *              4. 纯C接口封装，上层C业务无需感知底层C++实现
 *              5. 全局单例模式，适配服务化架构
 * 
 * @core_features 核心特性
 *              ✅ 外部BGR缓存【自动赋值】，无需上层手动配置（杜绝野指针/越界）
 *              ✅ 摄像头YUYV数据【只读访问】，绝对不修改采集链路内存
 *              ✅ AI推理内存【物理隔离】，独立使用AI专属FrameLink缓存
 *              ✅ 接口极简设计，上层仅需调用1个推理函数
 * 
 * @usage_scope 适用场景
 *              USB摄像头采集 + FrameLink内存池 + 嵌入式AI人脸检测业务
 * 
 * @author      Vision AI Framework
 * @date        2026
 * ==================================================================================
 */

/**
 * @defgroup ai_model_mnn MNN人脸检测模型（纯C接口）
 * @brief 基于MNN的UltraFace人脸检测算法实现，遵循AI模型通用基类接口
 *        支持外部BGR缓存隔离（AI专属数据链路），零拷贝操作FrameLink数据
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

// ==============================================================================
// ========================== 错误码定义（完整注释） ===========================
// ==============================================================================
/**
 * @enum MNN人脸检测执行状态错误码
 * @brief 所有接口的返回值定义，与底层基类接口完全兼容
 */
#define MNN_FACE_OK             0    /**< 执行成功：模型初始化/图像推理/数据输出 正常 */
#define MNN_FACE_ERR_INIT       -1   /**< 初始化失败：模型加载/内存申请失败 */
#define MNN_FACE_ERR_MODEL      -2   /**< 模型文件异常：路径错误/文件损坏 */
#define MNN_FACE_ERR_INPUT      -3   /**< 输入参数非法：空指针/分辨率不匹配 */
#define MNN_FACE_ERR_INFER      -4   /**< 推理执行失败：MNN内核运行异常 */

// ==============================================================================
// ========================== 模型默认配置（平台最优） =========================
// ==============================================================================
/**
 * @brief IMX6ULL平台 模型默认配置（训练分辨率/阈值）
 * @note  禁止随意修改，修改会导致检测精度/性能下降
 */
#define DEFAULT_AI_W            320    /**< 模型输入宽度（训练分辨率） */
#define DEFAULT_AI_H            240    /**< 模型输入高度（训练分辨率） */
#define DEFAULT_SCORE_THRESH    0.65f  /**< 人脸置信度阈值：低于该值过滤无效框 */
#define DEFAULT_IOU_THRESH      0.3f   /**< NMS非极大值抑制阈值：去重人脸框 */

// ==============================================================================
// ========================== 业务数据结构（上层专用） ========================
// ==============================================================================
/**
 * @struct FaceInfo_C
 * @brief  纯C人脸检测结果结构体
 * @details 上层业务唯一使用的人脸数据格式
 *          坐标为模型输出的原始坐标(320x240)，需调用映射接口转换为摄像头原始分辨率
 * 
 * @note   坐标范围：x1/y1(左上角) → x2/y2(右下角)
 * @note   置信度范围：0.0 ~ 1.0，数值越高可信度越高
 */
typedef struct {
    float x1;      /**< 人脸框 左上角X坐标（模型原始坐标） */
    float y1;      /**< 人脸框 左上角Y坐标（模型原始坐标） */
    float x2;      /**< 人脸框 右下角X坐标（模型原始坐标） */
    float y2;      /**< 人脸框 右下角Y坐标（模型原始坐标） */
    float score;   /**< 人脸置信度 0.0~1.0 */
} FaceInfo_C;

// ==============================================================================
// ========================== 核心对外接口（完整注释） ==========================
// ==============================================================================

/**
 * @brief  创建MNN人脸检测模型实例（全局单例）
 * @param  config: 模型配置结构体（路径/输入尺寸/置信度/IOU阈值）
 * @return 成功返回模型句柄，失败返回NULL
 * @note   1. 服务初始化时调用【仅调用1次】
 *          2. 全局单例模式，不支持多实例
 *          3. 必须在系统启动阶段初始化
 */
ai_model_handle_t* ai_model_mnn_create(const ai_model_config_t* config);

/**
 * @brief  人脸坐标等比例映射
 * @param  face: 模型输出的人脸结果
 * @param  cam_w: 摄像头原始宽度（如640）
 * @param  cam_h: 摄像头原始高度（如360）
 * @return 无返回值
 * @note   1. 将模型(320x240)坐标 → 摄像头原始分辨率坐标
 *          2. 检测到人脸后必须调用该接口，否则坐标不匹配
 */
void ai_model_mnn_map_face(FaceInfo_C* face, int cam_w, int cam_h);

/**
 * @brief  获取模型输入分辨率
 * @param  w: 输出模型宽度
 * @param  h: 输出模型高度
 * @return 无返回值
 */
void ai_model_mnn_get_ai_size(int* w, int* h);

/**
 * @brief  检查模型是否初始化完成（就绪状态）
 * @return true: 模型初始化完成，可执行推理
 *         false: 未初始化/异常
 */
bool ai_model_mnn_is_ready(void);

/**
 * @brief  【核心推理接口】YUYV图像人脸检测（自动管理外部BGR缓存）
 * 
 * @param  yuyv_data: 摄像头原始YUYV数据指针（来自FrameLink只读指针）
 * @param  cam_w: 摄像头原始宽度
 * @param  cam_h: 摄像头原始高度
 * @param  external_bgr_buf: 外部BGR缓存（AI专属FrameLink内存，自动管理）
 * @param  out_faces: 人脸检测结果输出数组
 * @param  max_faces: 最大支持检测人脸数量
 * @param  out_face_num: 输出实际检测到的人脸数量
 * 
 * @return MNN_FACE_OK: 推理成功
 *         其他: 执行失败（参考错误码）
 * 
 * @warning 【重要使用规范】
 *          1. ✅ 外部BGR缓存【自动赋值】，无需上层手动调用任何set接口
 *          2. ✅ yuyv_data 为只读数据，底层绝对不修改主采集链路内存
 *          3. ✅ BGR缓存必须来自AI专属FrameLink，实现物理内存隔离
 *          4. 禁止传递野指针/局部变量内存
 * 
 * @details 【内存安全保障】
 *          1. 采集链路(YUYV)：只读，无写入 → 无踩踏
 *          2. AI链路(BGR)：独立缓存，自动管理 → 无越界
 */
int ai_model_mnn_infer_yuyv(const uint8_t* yuyv_data, int cam_w, int cam_h,
                            uint8_t* external_bgr_buf,
                            FaceInfo_C* out_faces, int max_faces, int* out_face_num);

// ==============================================================================
// ========================== 快速上手示例（上层直接复制） =====================
// ==============================================================================
/*
--- 上层业务使用模板 ---
1. 初始化模型（服务init）
ai_model_config_t cfg = {
    .model_path = "/mnt/face.mnn",
    .input_width = DEFAULT_AI_W,
    .input_height = DEFAULT_AI_H,
    .score_thresh = DEFAULT_SCORE_THRESH,
    .iou_thresh = DEFAULT_IOU_THRESH,
};
ai_model_mnn_create(&cfg);

2. 帧推理（工作线程）
const uint8_t* yuyv = frame_get_readonly_ptr(yuyv_frame);
uint8_t* bgr_buf = frame_get_writable_ptr(rgb_frame);
FaceInfo_C faces[10];
int face_num = 0;

// 🔥 仅需调用这一个函数，自动处理所有内存！
int ret = ai_model_mnn_infer_yuyv(yuyv, 640, 360, bgr_buf, faces, 10, &face_num);

3. 坐标映射 + 业务处理
for (int i=0; i<face_num; i++) {
    ai_model_mnn_map_face(&faces[i], 640, 360);
}
*/

#ifdef __cplusplus
}
#endif

/** @} */

#endif // __AI_MODEL_MNN_HPP