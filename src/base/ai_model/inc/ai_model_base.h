#ifndef __AI_MODEL_BASE_H
#define __AI_MODEL_BASE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 统一错误码（所有AI模型通用）
typedef enum {
    AI_MODEL_OK        = 0,
    AI_MODEL_ERR_PARAM = -1,
    AI_MODEL_ERR_INIT  = -2,
    AI_MODEL_ERR_LOAD  = -3,
    AI_MODEL_ERR_INFER = -4,
    AI_MODEL_ERR_NO_MEM = -5,
} ai_model_err_t;

// 统一AI配置（所有模型通用：宽、高、模型路径、阈值）
typedef struct {
    const char *model_path;   // 模型文件路径
    uint32_t    input_width;  // 模型要求输入宽
    uint32_t    input_height; // 模型要求输入高
    float       score_thresh; // 置信度阈值
    float       iou_thresh;   // 非极大值抑制阈值
} ai_model_config_t;

// 统一检测结果结构（通用：人脸/目标检测）
typedef struct {
    float x1, y1, x2, y2;     // 坐标
    float score;              // 置信度
    int   class_id;           // 类别
} ai_model_detect_result_t;

// --------------------- C-OOP 基类核心（虚函数接口）---------------------
typedef struct ai_model_ops ai_model_ops_t;

// AI模型基类句柄（对外隐藏实现）
typedef struct {
    void *user_data;                  // 子类私有数据（MNN/RKNN...）
    const ai_model_ops_t *ops;        // 操作接口（多态核心）
    ai_model_config_t config;         // 配置
} ai_model_handle_t;

// 通用虚函数表（所有AI推理库必须实现这5个接口）
struct ai_model_ops {
    // 1. 初始化
    ai_model_err_t (*init)(ai_model_handle_t *handle);

    // 2. 输入图像数据（YUYV/NV12/RGB 通用）
    ai_model_err_t (*input)(ai_model_handle_t *handle,
                            uint8_t *data, uint32_t len);

    // 3. 执行推理
    ai_model_err_t (*infer)(ai_model_handle_t *handle);

    // 4. 获取推理结果
    ai_model_err_t (*get_result)(ai_model_handle_t *handle,
                                 ai_model_detect_result_t *results,
                                 uint32_t *result_count);

    // 5. 反初始化
    ai_model_err_t (*deinit)(ai_model_handle_t *handle);
};

// --------------------- 对外通用API ---------------------
#ifdef __cplusplus
extern "C" {
#endif

// 创建AI模型基类
ai_model_handle_t *ai_model_create(const ai_model_config_t *config,
                                   const ai_model_ops_t *ops);

// 销毁AI模型
void ai_model_destroy(ai_model_handle_t *handle);

// 以下API直接调用虚函数，上层无感知
ai_model_err_t ai_model_init(ai_model_handle_t *handle);
ai_model_err_t ai_model_input(ai_model_handle_t *handle, uint8_t *data, uint32_t len);
ai_model_err_t ai_model_infer(ai_model_handle_t *handle);
ai_model_err_t ai_model_get_result(ai_model_handle_t *handle,
                                   ai_model_detect_result_t *results,
                                   uint32_t *result_count);
ai_model_err_t ai_model_deinit(ai_model_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif // __AI_MODEL_BASE_H