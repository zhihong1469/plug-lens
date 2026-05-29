/* SPDX-License-Identifier: MIT */
/**
 * @file    ai_model_base.c
 * @brief   AI Model Base Class Implementation
 * @details Internal implementation:
 *          - C-OOP polymorphic interface dispatch
 *          - Universal memory management via mem_adapter
 *          - Automatic resource release mechanism
 *          - Unified parameter validation for all backends
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */

#include "ai_model_base.h"
#include "mem_adapter.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief   Create AI model instance (Base class implementation)
 * @details Allocate handle memory, copy configuration, initialize function table
 */
ai_model_handle_t *ai_model_create(const ai_model_config_t *config,
                                   const ai_model_ops_t *ops)
{
    // Parameter validation
    if (!config || !ops) {
        return NULL;
    }

    // Allocate memory using system memory adapter
    ai_model_handle_t *handle = (ai_model_handle_t *)mem_alloc(sizeof(ai_model_handle_t));
    if (!handle) {
        return NULL;
    }

    // Initialize handle members
    memcpy(&handle->config, config, sizeof(ai_model_config_t));
    handle->ops = ops;
    handle->user_data = NULL;

    return handle;
}

/**
 * @brief   Destroy AI model instance
 * @details Auto deinitialize before memory release to avoid resource leakage
 */
void ai_model_destroy(ai_model_handle_t *handle)
{
    if (handle) {
        // Auto release resources to prevent memory leak
        ai_model_deinit(handle);
        mem_free(handle);
    }
}

/**
 * @brief   Polymorphic wrapper: Initialize model
 * @details Validate handle and dispatch to subclass init implementation
 */
ai_model_err_t ai_model_init(ai_model_handle_t *handle)
{
    if (!handle || !handle->ops || !handle->ops->init) {
        return AI_MODEL_ERR_PARAM;
    }
    return handle->ops->init(handle);
}

/**
 * @brief   Polymorphic wrapper: Input image data
 * @details Validate parameters and dispatch to subclass input implementation
 */
ai_model_err_t ai_model_input(ai_model_handle_t *handle, uint8_t *data, uint32_t len)
{
    if (!handle || !handle->ops || !handle->ops->input) {
        return AI_MODEL_ERR_PARAM;
    }
    return handle->ops->input(handle, data, len);
}

/**
 * @brief   Polymorphic wrapper: Run inference
 * @details Validate handle and dispatch to subclass infer implementation
 */
ai_model_err_t ai_model_infer(ai_model_handle_t *handle)
{
    if (!handle || !handle->ops || !handle->ops->infer) {
        return AI_MODEL_ERR_PARAM;
    }
    return handle->ops->infer(handle);
}

/**
 * @brief   Polymorphic wrapper: Get detection results
 * @details Validate parameters and dispatch to subclass get_result implementation
 */
ai_model_err_t ai_model_get_result(ai_model_handle_t *handle,
                                   ai_model_detect_result_t *results,
                                   uint32_t *result_count)
{
    if (!handle || !handle->ops || !handle->ops->get_result) {
        return AI_MODEL_ERR_PARAM;
    }
    return handle->ops->get_result(handle, results, result_count);
}

/**
 * @brief   Polymorphic wrapper: Deinitialize model
 * @details Validate handle and dispatch to subclass deinit implementation
 */
ai_model_err_t ai_model_deinit(ai_model_handle_t *handle)
{
    if (!handle || !handle->ops || !handle->ops->deinit) {
        return AI_MODEL_ERR_PARAM;
    }
    return handle->ops->deinit(handle);
}