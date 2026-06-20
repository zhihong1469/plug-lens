#include "ai_model_factory.h"
#include "board_option.h"

#if AI_ENGINE_MNN
#include "ai_model_mnn.hpp"
extern const ai_model_ops_t ai_model_mnn_ops;
#elif AI_ENGINE_RKNN
#include "ai_model_rknn.hpp"
extern const ai_model_ops_t ai_model_rknn_ops;
#endif

ai_model_handle_t *ai_model_factory_create(const ai_model_config_t *config)
{
    if (!config) {
        return NULL;
    }

#if AI_ENGINE_MNN
    return ai_model_create(config, &ai_model_mnn_ops);
#elif AI_ENGINE_RKNN
    return ai_model_create(config, &ai_model_rknn_ops);
#else
    return NULL;
#endif
}

void ai_model_factory_destroy(ai_model_handle_t *handle)
{
    if (handle) {
        ai_model_destroy(handle);
    }
}