// src/fsm/module_fsm/src/module_fsm.c
#include "module_fsm.h"
#include <stdlib.h>
#include <string.h>

// ==========================================================================
// 内部状态结构体（完全封装）
// ==========================================================================
typedef struct {
    char module_name[32];
    module_state_t current_state;
    module_state_trans_t *trans_table;
    uint32_t trans_table_len;
    module_state_change_cb_t state_cb;
    module_event_handler_t event_handler;
    void *user_data;
    pthread_mutex_t lock; // 状态迁移锁，保证原子性
} module_fsm_context_t;

// ==========================================================================
// 状态/事件字符串映射表（可视化用）
// ==========================================================================
static const char* g_module_state_str[] = {
    [MODULE_STATE_INVALID] = "INVALID",
    [MODULE_STATE_IDLE] = "IDLE",
    [MODULE_STATE_READY] = "READY",
    [MODULE_STATE_RUNNING] = "RUNNING",
    [MODULE_STATE_PAUSED] = "PAUSED",
    [MODULE_STATE_ERROR] = "ERROR",
    [MODULE_STATE_DEINIT] = "DEINIT",
};

static const char* g_module_event_str[] = {
    [MODULE_EVENT_INVALID] = "INVALID",
    [MODULE_EVENT_INIT_OK] = "INIT_OK",
    [MODULE_EVENT_START] = "START",
    [MODULE_EVENT_START_OK] = "START_OK",
    [MODULE_EVENT_PAUSE] = "PAUSE",
    [MODULE_EVENT_PAUSE_OK] = "PAUSE_OK",
    [MODULE_EVENT_RESUME] = "RESUME",
    [MODULE_EVENT_RESUME_OK] = "RESUME_OK",
    [MODULE_EVENT_STOP] = "STOP",
    [MODULE_EVENT_STOP_OK] = "STOP_OK",
    [MODULE_EVENT_ERROR] = "ERROR",
    [MODULE_EVENT_DEINIT] = "DEINIT",
};

// ==========================================================================
// 内部辅助函数
// ==========================================================================
static bool _module_fsm_check_trans(module_fsm_context_t *ctx,
                                     module_event_t event,
                                     module_state_t *out_next_state)
{
    for (uint32_t i = 0; i < ctx->trans_table_len; i++) {
        if (ctx->trans_table[i].current_state == ctx->current_state
            && ctx->trans_table[i].event == event) {
            *out_next_state = ctx->trans_table[i].next_state;
            return true;
        }
    }
    return false;
}

// ==========================================================================
// 对外API实现
// ==========================================================================

const char* module_state_to_str(module_state_t state)
{
    if (state < 0 || state >= MODULE_STATE_MAX) {
        return g_module_state_str[MODULE_STATE_INVALID];
    }
    return g_module_state_str[state];
}

const char* module_event_to_str(module_event_t event)
{
    if (event < 0 || event >= MODULE_EVENT_MAX) {
        return g_module_event_str[MODULE_EVENT_INVALID];
    }
    return g_module_event_str[event];
}

int module_fsm_create(const module_fsm_config_t *config, module_fsm_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL || config->module_name == NULL) {
        return -1;
    }

    // 分配上下文
    module_fsm_context_t *ctx = (module_fsm_context_t*)malloc(sizeof(module_fsm_context_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(module_fsm_context_t));

    // 拷贝配置
    strncpy(ctx->module_name, config->module_name, sizeof(ctx->module_name) - 1);
    ctx->trans_table = config->trans_table;
    ctx->trans_table_len = config->trans_table_len;
    ctx->state_cb = config->state_cb;
    ctx->event_handler = config->event_handler;
    ctx->user_data = config->user_data;
    ctx->current_state = MODULE_STATE_IDLE; // 初始状态

    // 初始化锁（保证状态迁移原子性）
    pthread_mutex_init(&ctx->lock, NULL);

    *out_handle = (module_fsm_handle_t)ctx;
    return 0;
}

int module_fsm_destroy(module_fsm_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    module_fsm_context_t *ctx = (module_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    ctx->current_state = MODULE_STATE_DEINIT;
    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    return 0;
}

int module_fsm_post_event(module_fsm_handle_t handle, module_event_t event)
{
    if (handle == NULL || event == MODULE_EVENT_INVALID) {
        return -1;
    }
    module_fsm_context_t *ctx = (module_fsm_context_t*)handle;

    // 全程加锁，保证状态迁移原子性
    pthread_mutex_lock(&ctx->lock);

    // 1. 执行事件处理回调（仅决策，不执行业务）
    if (ctx->event_handler != NULL) {
        int ret = ctx->event_handler(event, ctx->user_data);
        if (ret != 0) {
            pthread_mutex_unlock(&ctx->lock);
            return ret; // 事件处理不通过，禁止跳转
        }
    }

    // 2. 校验状态迁移规则
    module_state_t next_state = MODULE_STATE_INVALID;
    if (!_module_fsm_check_trans(ctx, event, &next_state)) {
        pthread_mutex_unlock(&ctx->lock);
        return -1; // 非法状态跳转
    }

    // 3. 执行状态变更
    module_state_t old_state = ctx->current_state;
    ctx->current_state = next_state;
    pthread_mutex_unlock(&ctx->lock);

    // 4. 通知状态变更（回调总线/全局状态机）
    if (ctx->state_cb != NULL) {
        ctx->state_cb(ctx->module_name, old_state, next_state, ctx->user_data);
    }

    return 0;
}

module_state_t module_fsm_get_state(module_fsm_handle_t handle)
{
    if (handle == NULL) {
        return MODULE_STATE_INVALID;
    }
    module_fsm_context_t *ctx = (module_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    module_state_t state = ctx->current_state;
    pthread_mutex_unlock(&ctx->lock);

    return state;
}

const char* module_fsm_get_name(module_fsm_handle_t handle)
{
    if (handle == NULL) {
        return NULL;
    }
    module_fsm_context_t *ctx = (module_fsm_context_t*)handle;
    return ctx->module_name;
}
