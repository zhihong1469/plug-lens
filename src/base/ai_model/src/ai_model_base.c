#include "ai_model_base.h"
#include <stdlib.h>
#include <string.h>

ai_model_handle_t *ai_model_create(const ai_model_config_t *config,
                                   const ai_model_ops_t *ops)
{
    if (!config || !ops) return NULL;

    ai_model_handle_t *handle = (ai_model_handle_t *)mem_alloc(sizeof(ai_model_handle_t));
    if (!handle) return NULL;

    memcpy(&handle->config, config, sizeof(ai_model_config_t));
    handle->ops = ops;
    handle->user_data = NULL;

    return handle;
}

void ai_model_destroy(ai_model_handle_t *handle)
{
    if (handle) {
        ai_model_deinit(handle);
        mem_free(handle);
    }
}

ai_model_err_t ai_model_init(ai_model_handle_t *handle)
{
    if (!handle || !handle->ops || !handle->ops->init)
        return AI_MODEL_ERR_PARAM;
    return handle->ops->init(handle);
}

ai_model_err_t ai_model_input(ai_model_handle_t *handle, uint8_t *data, uint32_t len)
{
    if (!handle || !handle->ops || !handle->ops->input)
        return AI_MODEL_ERR_PARAM;
    return handle->ops->input(handle, data, len);
}

ai_model_err_t ai_model_infer(ai_model_handle_t *handle)
{
    if (!handle || !handle->ops || !handle->ops->infer)
        return AI_MODEL_ERR_PARAM;
    return handle->ops->infer(handle);
}

ai_model_err_t ai_model_get_result(ai_model_handle_t *handle,
                                   ai_model_detect_result_t *results,
                                   uint32_t *result_count)
{
    if (!handle || !handle->ops || !handle->ops->get_result)
        return AI_MODEL_ERR_PARAM;
    return handle->ops->get_result(handle, results, result_count);
}

ai_model_err_t ai_model_deinit(ai_model_handle_t *handle)
{
    if (!handle || !handle->ops || !handle->ops->deinit)
        return AI_MODEL_ERR_PARAM;
    return handle->ops->deinit(handle);
}