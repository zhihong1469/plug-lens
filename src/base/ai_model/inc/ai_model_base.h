/* SPDX-License-Identifier: MIT */
/**
 * @file    ai_model_base.h
 * @brief   AI Model Universal Abstract Base Class
 * @details C-OOP polymorphic design for cross-inference engine AI models (MNN/RKNN/TFLite).
 *          Provides unified abstract interface, business layer is unaware of underlying inference engine differences.
 *          Supports object/face detection with universal input/output formats.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All functions are not thread-safe unless marked specially.
 *          2. Strict lifecycle: create → init → input → infer → get_result → deinit → destroy.
 *          3. Only one instance is allowed in a single process.
 *          4. create() only allocates handle, init() loads model and initializes hardware.
 */

/**
 * @defgroup ai_model_base AI Model Universal Abstract Base Class
 * @brief   Cross-inference library unified abstract interface, pure C polymorphic design
 * @details Shield inference engine differences for upper business layer
 * 
 */

#ifndef __AI_MODEL_BASE_H
#define __AI_MODEL_BASE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif



// ==========================
// Universal Error Code (All AI Models)
// ==========================
/**
 * @brief   AI model universal return error code
 * @details Standardized error codes for all AI inference backends
 */
typedef enum {
    AI_MODEL_OK        = 0,    /**< Operation completed successfully */
    AI_MODEL_ERR_PARAM = -1,   /**< Invalid input parameter (NULL/out of range) */
    AI_MODEL_ERR_INIT  = -2,   /**< Module initialization failure */
    AI_MODEL_ERR_LOAD  = -3,   /**< Model file loading/parsing failure */
    AI_MODEL_ERR_INFER = -4,   /**< Inference execution runtime failure */
    AI_MODEL_ERR_NO_MEM = -5,  /**< Memory allocation failure */
} ai_model_err_t;

// ==========================
// Universal AI Configuration (All Models)
// ==========================
/**
 * @brief   AI model universal configuration structure
 * @details Shared configuration parameters for all detection models
 * @note    Used for instance creation and initialization
 */
typedef struct {
    const char      *model_path;       /**< File path of AI model binary */
    uint32_t         input_width;      /**< Model input image width (pixels) */
    uint32_t         input_height;     /**< Model input image height (pixels) */
    float            score_thresh;     /**< Confidence threshold (0.0~1.0) */
    float            iou_thresh;       /**< NMS IoU threshold (0.0~1.0) */
} ai_model_config_t;

// ==========================
// Universal Detection Result Structure
// ==========================
/**
 * @brief   AI universal object detection result
 * @details Standard output format for face/object detection tasks
 * @note    Coordinates are normalized or pixel-based (backend defined)
 */
typedef struct {
    float x1, y1, x2, y2;     /**< Bounding box: top-left (x1,y1) / bottom-right (x2,y2) */
    float score;              /**< Detection confidence score (0.0 ~ 1.0) */
    int   class_id;           /**< Target class ID (0 = face for face detection) */
} ai_model_detect_result_t;

// --------------------- C-OOP Base Class Core ---------------------
/**
 * @brief   Forward declaration of AI model operation table
 */
typedef struct ai_model_ops ai_model_ops_t;

/**
 * @brief   AI model base class handle
 * @details Opaque handle for upper layer usage, internal implementation hidden
 * @note    1. Do not modify internal members directly, use public APIs only
 *          2. user_data stores backend private context (MNN/RKNN handles)
 */
typedef struct {
    const ai_model_ops_t *ops;        /**< Polymorphic operation function table */
    ai_model_config_t config;         /**< Copied model configuration */
    void *user_data;                  /**< Subclass private data pointer */
} ai_model_handle_t;

/**
 * @brief   AI model virtual function table
 * @details Mandatory interfaces for all inference backends to implement
 * @note    Pure virtual interface, no default implementation
 */
struct ai_model_ops {
    ai_model_err_t (*init)(ai_model_handle_t *handle);            /**< Initialize model & hardware */
    ai_model_err_t (*input)(ai_model_handle_t *handle, uint8_t *data, uint32_t len); /**< Input image data */
    ai_model_err_t (*infer)(ai_model_handle_t *handle);           /**< Run model inference */
    ai_model_err_t (*get_result)(ai_model_handle_t *handle, ai_model_detect_result_t *results, uint32_t *result_count); /**< Get detection results */
    ai_model_err_t (*deinit)(ai_model_handle_t *handle);          /**< Release model resources */
};

// --------------------- Public Universal API ---------------------
/**
 * @brief   Create AI model instance
 * @param   config  Pointer to model configuration, cannot be NULL
 * @param   ops     Pointer to subclass operation table, cannot be NULL
 * @return  Valid handle on success, NULL on failure (memory/param error)
 *
 * @pre     No preconditions
 * @post    Handle allocated, config copied, no model loaded
 *
 * @note    Only allocates memory, does NOT load model or initialize hardware
 * @warning Do not call infer/input before init()
 * @thread_safety No
 *
 * @example
 * @code
 * ai_model_config_t config = {
 *     .model_path = "/app/model.rknn",
 *     .input_width = 640,
 *     .input_height = 640,
 *     .score_thresh = 0.5f,
 *     .iou_thresh = 0.45f
 * };
 * ai_model_handle_t *handle = ai_model_create(&config, rknn_model_ops);
 * @endcode
 */
ai_model_handle_t *ai_model_create(const ai_model_config_t *config, const ai_model_ops_t *ops);

/**
 * @brief   Destroy AI model instance
 * @param   handle  Model instance handle, can be NULL
 * @return  None
 *
 * @pre     None (auto calls deinit internally)
 * @post    All resources released, handle invalidated
 *
 * @note    Auto triggers deinit before freeing memory
 * @thread_safety No
 */
void ai_model_destroy(ai_model_handle_t *handle);

/**
 * @brief   Initialize AI model
 * @param   handle  Model instance handle, cannot be NULL
 * @return  ai_model_err_t: 0=success, negative=error code
 *
 * @pre     Instance created via ai_model_create()
 * @post    Model loaded, hardware initialized, ready for inference
 *
 * @warning Call only once per instance
 * @thread_safety No
 */
ai_model_err_t ai_model_init(ai_model_handle_t *handle);

/**
 * @brief   Input image data to model
 * @param   handle  Model instance handle, cannot be NULL
 * @param   data    Image data buffer (YUYV/RGB/NV12), cannot be NULL
 * @param   len     Image data buffer length (bytes)
 * @return  ai_model_err_t: 0=success, negative=error code
 *
 * @pre     Model initialized successfully
 * @post    Image data loaded into model input tensor
 *
 * @note    Supports YUYV/RGB/NV12 formats (backend dependent)
 * @thread_safety No
 */
ai_model_err_t ai_model_input(ai_model_handle_t *handle, uint8_t *data, uint32_t len);

/**
 * @brief   Execute model inference
 * @param   handle  Model instance handle, cannot be NULL
 * @return  ai_model_err_t: 0=success, negative=error code
 *
 * @pre     Image data input via ai_model_input()
 * @post    Inference completed, results ready to read
 *
 * @thread_safety No
 */
ai_model_err_t ai_model_infer(ai_model_handle_t *handle);

/**
 * @brief   Get model detection results
 * @param   handle        Model instance handle, cannot be NULL
 * @param   results       Output result array, cannot be NULL
 * @param   result_count  Output: actual number of detected objects, cannot be NULL
 * @return  ai_model_err_t: 0=success, negative=error code
 *
 * @pre     Inference completed via ai_model_infer()
 * @post    Results copied to output array
 *
 * @thread_safety No
 */
ai_model_err_t ai_model_get_result(ai_model_handle_t *handle, ai_model_detect_result_t *results, uint32_t *result_count);

/**
 * @brief   Deinitialize AI model
 * @param   handle  Model instance handle, cannot be NULL
 * @return  ai_model_err_t: 0=success, negative=error code
 *
 * @pre     Model initialized
 * @post    Model unloaded, hardware resources released
 *
 * @note    Called automatically by ai_model_destroy()
 * @thread_safety No
 */
ai_model_err_t ai_model_deinit(ai_model_handle_t *handle);

/** @} */ // end of ai_model_base

#ifdef __cplusplus
}
#endif

#endif // __AI_MODEL_BASE_H