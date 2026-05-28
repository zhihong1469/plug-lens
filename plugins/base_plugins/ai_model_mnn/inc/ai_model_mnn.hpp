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
 *              3. 直接对接FrameLink数据链路，零拷贝读取通用原始图像
 *              4. 纯C接口封装，上层C业务无需感知底层C++实现
 *              5. 全局单例模式，适配服务化架构
 *              6. 新增：OpenCV原生人脸框绘制接口
 *              7. 通用化设计：兼容 YUYV / MJPEG 双图像格式输入
 * 
 * @core_features 核心特性
 *              ✅ 外部BGR缓存【自动赋值】，无需上层手动配置（杜绝野指针/越界）
 *              ✅ 摄像头图像数据【只读访问】，绝对不修改采集链路内存
 *              ✅ AI推理内存【物理隔离】，独立使用AI专属FrameLink缓存
 *              ✅ 接口极简设计，上层仅需调用1个推理函数
 *              ✅ OpenCV原生绘制人脸框，高效简洁
 *              ✅ 双格式兼容：支持YUYV/MJPEG硬件图像输入
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
#define MNN_FACE_OK             0    /**< 执行成功：模型初始化/图像推理/数据输出 正常 */
#define MNN_FACE_ERR_INIT       -1   /**< 初始化失败：模型加载/内存申请失败 */
#define MNN_FACE_ERR_MODEL      -2   /**< 模型文件异常：路径错误/文件损坏 */
#define MNN_FACE_ERR_INPUT      -3   /**< 输入参数非法：空指针/分辨率不匹配 */
#define MNN_FACE_ERR_INFER      -4   /**< 推理执行失败：MNN内核运行异常 */

// ==============================================================================
// ========================== 输入格式枚举（通用） ==============================
// ==============================================================================
#define INPUT_FORMAT            0    /**< 输入图像格式：0=YUYV 1=MJPEG（硬件默认，高性能） */


// 绘制配置（画框）
#define FACE_BOX_THICKNESS      2        /**< 人脸框粗细 */
#define FACE_BOX_COLOR_BLUE     0xFF0000  /**< 静止人脸：蓝色 */
#define FACE_BOX_COLOR_RED      0x0000FF  /**< 移动人脸：红色 */
#define FACE_BOX_COLOR_GREEN    0x00FF00  /**< 低置信度人脸：绿色 */
// 低置信度阈值：低于此值显示绿色框
#define FACE_LOW_SCORE_THRESH   0.6f
// 人脸静止判断阈值：坐标变化小于10像素视为未移动
#define FACE_STATIC_THRESHOLD    10.0f
// 最大支持人脸数
#define FACE_DETECT_MAX_FACES    10
// ==============================================================================
// ========================== 业务数据结构（上层专用） ========================
// ==============================================================================
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
ai_model_handle_t* ai_model_mnn_create(const ai_model_config_t* config);

/**
 * @brief  人脸坐标等比例映射
 * @param  face: 模型输出的人脸结果
 * @param  cam_w: 摄像头原始宽度（如640）
 * @param  cam_h: 摄像头原始高度（如360）
 * @return 无返回值
 */
void ai_model_mnn_map_face(FaceInfo_C* face, int cam_w, int cam_h);

/**
 * @brief  【新增】坐标映射 + OpenCV绘制人脸框（三合一）
 * @param  face: 人脸信息
 * @param  face_num: 人脸数量
 * @param  cam_w: 摄像头宽度
 * @param  cam_h: 摄像头高度
 * @param  src_frame: 原始图像帧数据
 * @param  dst_frame: 输出带框图像帧数据
 * @return 无
 */
int ai_model_mnn_map_and_draw_faces(FaceInfo_C* faces, int face_num, 
                                     int cam_w, int cam_h,
                                     const uint8_t *src_frame, uint8_t *dst_frame);

void ai_model_mnn_get_ai_size(int* w, int* h);
bool ai_model_mnn_is_ready(void);

/**
 * @brief  通用图像人脸检测推理接口（兼容YUYV/MJPEG）
 * @param  image_data: 摄像头输入图像数据
 * @param  cam_w: 摄像头宽度
 * @param  cam_h: 摄像头高度
 * @param  external_bgr_buf: 外部BGR缓存（AI推理专用）
 * @param  out_faces: 人脸检测结果输出
 * @param  max_faces: 最大支持人脸数量
 * @param  out_face_num: 实际检测到的人脸数量
 * @param  format: 图像输入格式 0=YUYV 1=MJPEG
 * @return 错误码
 */
int ai_model_mnn_infer_image(const uint8_t* image_data, int cam_w, int cam_h,
                            uint8_t* external_bgr_buf,
                            FaceInfo_C* out_faces, int max_faces, int* out_face_num,
                            uint8_t format);

// ==============================================================================
// ========================== 快速上手示例（上层直接复制） =====================
// ==============================================================================
/*
--- 上层业务使用模板（通用版：支持MJPEG/YUYV） ---
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
const uint8_t* camera_frame = frame_get_readonly_ptr(camera_frame);  // 通用图像
uint8_t* bgr_buf = frame_get_writable_ptr(rgb_frame);
FaceInfo_C faces[10];
int face_num = 0;

// 🔥 仅需调用这一个函数，自动适配解码格式！
// MJPEG格式（硬件默认，低CPU）：
// YUYV格式（兼容模式）：
ai_model_mnn_infer_image(..., INPUT_FORMAT)
int ret = ai_model_mnn_infer_image(camera_frame, 640, 360, bgr_buf, faces, 10, &face_num, INPUT_FORMAT);

3. 坐标映射 + 业务处理
for (int i=0; i<face_num; i++) {
    ai_model_mnn_map_and_draw_face(&faces[i], 640, 360, original_frame, bgr_buf);
}
*/

#ifdef __cplusplus
}
#endif

/** @} */

#endif // __AI_MODEL_MNN_HPP