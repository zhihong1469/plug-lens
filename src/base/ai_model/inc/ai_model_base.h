/* SPDX-License-Identifier: MIT */
/**
 * @file    ai_model_base.h
 * @brief   AI Model Universal Abstract Base Class
 * @details Cross-inference engine unified abstract interface (MNN/RKNN/TFLite),
 *          C-OOP polymorphic design, business layer is unaware of inference engine differences
 * @author  LuoZhihong
 * @date    2026-05-31
 *
 * @note
 * 1. AI Module Lifecycle
 *    create: Allocate handle & memory only, NO model loading
 *    init:   Load model, initialize hardware, assign global pointers (CORE STEP)
 *    infer:  Run model inference
 *    deinit: Release all resources
 */

#ifndef __AI_MODEL_BASE_H
#define __AI_MODEL_BASE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @defgroup ai_model_base AI Model Universal Abstract Base Class
 * @brief Cross-inference library unified abstract interface (MNN/RKNN/TFLite), C-OOP polymorphic design
 * @note  Pure C interface, upper business layer has no perception of inference library differences
 * @{
 */

// ==========================
// Universal Error Code (All AI Models)
// ==========================
/**
 * @brief AI model universal error code
 */
typedef enum {
    AI_MODEL_OK        = 0,    /**< Operation successful */
    AI_MODEL_ERR_PARAM = -1,   /**< Invalid input parameter */
    AI_MODEL_ERR_INIT  = -2,   /**< Module initialization failed */
    AI_MODEL_ERR_LOAD  = -3,   /**< Model file loading failed */
    AI_MODEL_ERR_INFER = -4,   /**< Inference execution failed */
    AI_MODEL_ERR_NO_MEM = -5,  /**< Memory allocation failed */
} ai_model_err_t;

// ==========================
// Universal AI Configuration (All Models)
// ==========================
/**
 * @brief AI model universal configuration structure
 * @note  Shared by all detection models (face/object detection)
 */
typedef struct {
    const char      *model_path;       /**< Model file path */
    uint32_t         input_width;      /**< Model input width */
    uint32_t         input_height;     /**< Model input height */
    float            score_thresh;     /**< Confidence threshold */
    float            iou_thresh;       /**< NMS threshold */
} ai_model_config_t;

// ==========================
// Universal Detection Result Structure
// ==========================
/**
 * @brief AI universal detection result
 * @note  Common output format for face/object detection
 */
typedef struct {
    float x1, y1, x2, y2;     /**< Bounding box coordinates (top-left / bottom-right) */
    float score;              /**< Confidence score 0~1 */
    int   class_id;           /**< Class ID (0 for face detection) */
} ai_model_detect_result_t;

// --------------------- C-OOP Base Class Core ---------------------
// Forward declaration
typedef struct ai_model_ops ai_model_ops_t;

/**
 * @brief AI model base class handle (Implementation hidden externally)
 * @note  Upper business layer only operates this handle, no internal details required
 */
typedef struct {
    const ai_model_ops_t *ops;        /**< Polymorphic operation function table */
    ai_model_config_t config;         /**< Model configuration copy */
    void *user_data;                  /**< Subclass private data (MNN/RKNN internal handles) */
} ai_model_handle_t;

/**
 * @brief Universal virtual function table (Must be implemented by inference backends)
 * @note  Unified interface contract for all AI inference backends
 */
struct ai_model_ops {
    /** @brief Initialize model (Load file, create resources) */
    ai_model_err_t (*init)(ai_model_handle_t *handle);
    /** @brief Input image data (Support YUYV/RGB/NV12) */
    ai_model_err_t (*input)(ai_model_handle_t *handle, uint8_t *data, uint32_t len);
    /** @brief Run inference */
    ai_model_err_t (*infer)(ai_model_handle_t *handle);
    /** @brief Get inference results */
    ai_model_err_t (*get_result)(ai_model_handle_t *handle, ai_model_detect_result_t *results, uint32_t *result_count);
    /** @brief Deinitialize (Release resources) */
    ai_model_err_t (*deinit)(ai_model_handle_t *handle);
};

// --------------------- Public Universal API ---------------------
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Create AI model instance
 * @param  config  Model configuration
 * @param  ops     Subclass operation function table
 * @return Handle on success, NULL on failure
 */
ai_model_handle_t *ai_model_create(const ai_model_config_t *config, const ai_model_ops_t *ops);

/**
 * @brief  Destroy AI model instance
 * @param  handle  Model handle
 */
void ai_model_destroy(ai_model_handle_t *handle);

/**
 * @brief  Initialize model (Call subclass init)
 * @param  handle  Model handle
 * @return Error code
 */
ai_model_err_t ai_model_init(ai_model_handle_t *handle);

/**
 * @brief  Input image data
 * @param  handle  Model handle
 * @param  data    Image data pointer
 * @param  len     Data length
 * @return Error code
 */
ai_model_err_t ai_model_input(ai_model_handle_t *handle, uint8_t *data, uint32_t len);

/**
 * @brief  Run model inference
 * @param  handle  Model handle
 * @return Error code
 */
ai_model_err_t ai_model_infer(ai_model_handle_t *handle);

/**
 * @brief  Get detection results
 * @param  handle        Model handle
 * @param  results       Result array
 * @param  result_count  Actual result count
 * @return Error code
 */
ai_model_err_t ai_model_get_result(ai_model_handle_t *handle, ai_model_detect_result_t *results, uint32_t *result_count);

/**
 * @brief  Deinitialize model
 * @param  handle  Model handle
 * @return Error code
 */
ai_model_err_t ai_model_deinit(ai_model_handle_t *handle);

#ifdef __cplusplus
}
#endif

/** @} */ // end of ai_model_base

#endif // __AI_MODEL_BASE_H