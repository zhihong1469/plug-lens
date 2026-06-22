#ifndef __AI_MODEL_FACTORY_H__
#define __AI_MODEL_FACTORY_H__

#include "ai_model_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Create AI model instance based on platform configuration
 * @details Automatically selects the appropriate AI engine (MNN/RKNN) 
 *          based on the BOARD_OPTION_H settings.
 * @param   config  Pointer to AI model configuration structure
 * @return  Valid AI model handle on success, NULL on failure
 * 
 * @pre     config must not be NULL and must contain valid parameters
 * @post    Handle is allocated but model is NOT loaded (call init() to load)
 * @note    Factory pattern hides implementation details from caller
 * @warning Caller is responsible for destroying the handle when done
 * 
 * @example
 * @code
 * ai_model_config_t config = {
 *     .model_path = "/app/model.mnn",
 *     .input_width = 640,
 *     .input_height = 640,
 *     .score_thresh = 0.5f,
 *     .iou_thresh = 0.45f
 * };
 * ai_model_handle_t *handle = ai_model_factory_create(&config);
 * if (handle) {
 *     handle->ops->init(handle);
 *     // ... use model ...
 *     handle->ops->deinit(handle);
 *     ai_model_factory_destroy(handle);
 * }
 * @endcode
 */
ai_model_handle_t *ai_model_factory_create(const ai_model_config_t *config);

/**
 * @brief   Destroy AI model instance
 * @param   handle  AI model handle to destroy
 * @note    Safe to call with NULL handle
 */
void ai_model_factory_destroy(ai_model_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* __AI_MODEL_FACTORY_H__ */