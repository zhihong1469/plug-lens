/*
1. AI 模块生命周期
create：仅创建句柄、分配内存不加载模型
init：真正加载模型、初始化硬件、赋值全局指针（核心步骤）
infer：执行推理
deinit：释放资源
*/
#ifndef __AI_MODEL_BASE_H
#define __AI_MODEL_BASE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @defgroup ai_model_base AI模型通用抽象基类
 * @brief 跨推理库统一抽象接口（MNN/RKNN/TFLite），C-OOP多态设计
 * @note  纯C接口，上层业务无感知推理库差异
 * @{
 */

// ==========================
// 统一错误码（所有AI模型通用）
// ==========================
/**
 * @brief AI模型通用错误码
 */
typedef enum {
    AI_MODEL_OK        = 0,    /**< 操作成功 */
    AI_MODEL_ERR_PARAM = -1,   /**< 输入参数错误 */
    AI_MODEL_ERR_INIT  = -2,   /**< 模块初始化失败 */
    AI_MODEL_ERR_LOAD  = -3,   /**< 模型文件加载失败 */
    AI_MODEL_ERR_INFER = -4,   /**< 推理执行失败 */
    AI_MODEL_ERR_NO_MEM = -5,  /**< 内存分配失败 */
} ai_model_err_t;

// ==========================
// 统一AI配置（所有模型通用）
// ==========================
/**
 * @brief AI模型通用配置结构体
 * @note  所有检测模型（人脸/目标）共用配置
 */
typedef struct {
    const char      *model_path;       // 模型路径 8/4B
    uint32_t         input_width;      // 输入宽度 4B
    uint32_t         input_height;     // 输入高度 4B
    float            score_thresh;     // 置信度阈值 4B
    float            iou_thresh;       // NMS阈值 4B
} ai_model_config_t;

// ==========================
// 统一检测结果结构
// ==========================
/**
 * @brief AI通用检测结果
 * @note  人脸检测、目标检测通用输出格式
 */
typedef struct {
    float x1, y1, x2, y2;     /**< 检测框坐标（左上角/右下角）*/
    float score;              /**< 置信度分数 0~1 */
    int   class_id;           /**< 类别ID（人脸固定为0）*/
} ai_model_detect_result_t;

// --------------------- C-OOP 基类核心 ---------------------
// 前向声明
typedef struct ai_model_ops ai_model_ops_t;

/**
 * @brief AI模型基类句柄（对外隐藏实现）
 * @note  上层业务仅操作该句柄，无需关心内部实现
 */
typedef struct {
    const ai_model_ops_t *ops;        /**< 多态操作函数表 */
    ai_model_config_t config;         /**< 模型配置副本 */
    void *user_data;                  /**< 子类私有数据（MNN/RKNN内部句柄）*/
} ai_model_handle_t;

/**
 * @brief 通用虚函数表（推理库必须实现）
 * @note  所有AI推理后端的统一接口契约
 */
struct ai_model_ops {
    /** @brief 初始化模型（加载文件、创建资源）*/
    ai_model_err_t (*init)(ai_model_handle_t *handle);
    /** @brief 输入图像数据（支持YUYV/RGB/NV12）*/
    ai_model_err_t (*input)(ai_model_handle_t *handle, uint8_t *data, uint32_t len);
    /** @brief 执行推理 */
    ai_model_err_t (*infer)(ai_model_handle_t *handle);
    /** @brief 获取推理结果 */
    ai_model_err_t (*get_result)(ai_model_handle_t *handle, ai_model_detect_result_t *results, uint32_t *result_count);
    /** @brief 反初始化（释放资源）*/
    ai_model_err_t (*deinit)(ai_model_handle_t *handle);
};

// --------------------- 对外通用API ---------------------
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建AI模型实例
 * @param config 模型配置
 * @param ops 子类操作函数表
 * @return 成功返回句柄，失败返回NULL
 */
ai_model_handle_t *ai_model_create(const ai_model_config_t *config, const ai_model_ops_t *ops);

/**
 * @brief 销毁AI模型实例
 * @param handle 模型句柄
 */
void ai_model_destroy(ai_model_handle_t *handle);

/**
 * @brief 初始化模型（调用子类init）
 * @param handle 模型句柄
 * @return 错误码
 */
ai_model_err_t ai_model_init(ai_model_handle_t *handle);

/**
 * @brief 输入图像数据
 * @param handle 模型句柄
 * @param data 图像数据指针
 * @param len 数据长度
 * @return 错误码
 */
ai_model_err_t ai_model_input(ai_model_handle_t *handle, uint8_t *data, uint32_t len);

/**
 * @brief 执行推理
 * @param handle 模型句柄
 * @return 错误码
 */
ai_model_err_t ai_model_infer(ai_model_handle_t *handle);

/**
 * @brief 获取检测结果
 * @param handle 模型句柄
 * @param results 结果数组
 * @param result_count 实际结果数量
 * @return 错误码
 */
ai_model_err_t ai_model_get_result(ai_model_handle_t *handle, ai_model_detect_result_t *results, uint32_t *result_count);

/**
 * @brief 反初始化模型
 * @param handle 模型句柄
 * @return 错误码
 */
ai_model_err_t ai_model_deinit(ai_model_handle_t *handle);

#ifdef __cplusplus
}
#endif

/** @} */ // end of ai_model_base

#endif // __AI_MODEL_BASE_H