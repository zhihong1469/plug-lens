// src/fsm/module_fsm/src/module_fsm.c
#include "module_fsm.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ==========================================================================
// 内部上下文结构体
// ==========================================================================
typedef struct {
    char module_name[32];
    module_state_t current_state;
    const module_state_trans_t *trans_table;
    uint32_t trans_table_len;
    module_event_handler_t event_handler;
    module_state_change_cb_t state_cb;
    void *user_data;
    pthread_mutex_t lock;
} module_fsm_context_t;

// ==========================================================================
// 字符串映射表
// ==========================================================================
static const char* g_module_state_str[] = {
    [MODULE_STATE_INVALID] = "INVALID",
    [MODULE_STATE_IDLE] = "IDLE",
    [MODULE_STATE_INITIALIZING] = "INITIALIZING",
    [MODULE_STATE_READY] = "READY",
    [MODULE_STATE_STARTING] = "STARTING",
    [MODULE_STATE_RUNNING] = "RUNNING",
    [MODULE_STATE_PAUSING] = "PAUSING",
    [MODULE_STATE_PAUSED] = "PAUSED",
    [MODULE_STATE_STOPPING] = "STOPPING",
    [MODULE_STATE_ERROR] = "ERROR",
    [MODULE_STATE_DEINITIALIZING] = "DEINITIALIZING",
    [MODULE_STATE_DEINIT] = "DEINIT",
};

static const char* g_module_event_str[] = {
    [MODULE_EVENT_INVALID] = "INVALID",
    [MODULE_EVENT_INIT] = "INIT",
    [MODULE_EVENT_INIT_OK] = "INIT_OK",
    [MODULE_EVENT_INIT_FAIL] = "INIT_FAIL",
    [MODULE_EVENT_START] = "START",
    [MODULE_EVENT_START_OK] = "START_OK",
    [MODULE_EVENT_START_FAIL] = "START_FAIL",
    [MODULE_EVENT_PAUSE] = "PAUSE",
    [MODULE_EVENT_PAUSE_OK] = "PAUSE_OK",
    [MODULE_EVENT_RESUME] = "RESUME",
    [MODULE_EVENT_RESUME_OK] = "RESUME_OK",
    [MODULE_EVENT_STOP] = "STOP",
    [MODULE_EVENT_STOP_OK] = "STOP_OK",
    [MODULE_EVENT_ERROR] = "ERROR",
    [MODULE_EVENT_ERROR_CLEAR] = "ERROR_CLEAR",
    [MODULE_EVENT_DEINIT] = "DEINIT",
    [MODULE_EVENT_DEINIT_OK] = "DEINIT_OK",
};

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static bool _module_fsm_check_trans(module_fsm_context_t *ctx,
                                     module_event_t event,
                                     module_state_t *out_next_state);

// ==========================================================================
// 对外API实现
// ==========================================================================

int module_fsm_create(const module_fsm_config_t *config,
                      module_fsm_handle_t *out_handle)
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
    ctx->event_handler = config->event_handler;
    ctx->state_cb = config->state_cb;
    ctx->user_data = config->user_data;

    // 初始状态
    ctx->current_state = MODULE_STATE_IDLE;

    // 初始化锁
    pthread_mutex_init(&ctx->lock, NULL);

    *out_handle = (module_fsm_handle_t)ctx;
    LOG_I("Module FSM: [%s] created, initial state: %s", 
          ctx->module_name, module_state_to_str(ctx->current_state));
    return 0;
}

int module_fsm_post_event(module_fsm_handle_t handle, module_event_t event)
{
    if (handle == NULL || event == MODULE_EVENT_INVALID || event >= MODULE_EVENT_MAX) {
        return -1;
    }
    module_fsm_context_t *ctx = (module_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    LOG_I("Module FSM: [%s] received event: %s (current state: %s)", 
          ctx->module_name, 
          module_event_to_str(event), 
          module_state_to_str(ctx->current_state));

    // 1. 检查状态迁移规则
    module_state_t next_state = MODULE_STATE_INVALID;
    if (!_module_fsm_check_trans(ctx, event, &next_state)) {
        LOG_W("Module FSM: [%s] invalid transition: %s + %s", 
              ctx->module_name, 
              module_state_to_str(ctx->current_state), 
              module_event_to_str(event));
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    // 2. 调用业务层的事件处理回调（如果有）
    // 注意：这里在锁里调用回调，业务层回调应尽量快，只做决策，不做耗时操作
    int ret = 0;
    if (ctx->event_handler != NULL) {
        ret = ctx->event_handler(event, ctx->user_data);
        if (ret != 0) {
            LOG_E("Module FSM: [%s] event handler failed for event %s, ret=%d", 
                  ctx->module_name, module_event_to_str(event), ret);
            pthread_mutex_unlock(&ctx->lock);
            return ret; // 业务层禁止迁移
        }
    }

    // 3. 执行状态迁移
    module_state_t old_state = ctx->current_state;
    ctx->current_state = next_state;

    LOG_I("Module FSM: [%s] state changed: %s -> %s", 
          ctx->module_name, 
          module_state_to_str(old_state), 
          module_state_to_str(next_state));

    pthread_mutex_unlock(&ctx->lock);

    // 4. 调用上层的状态变化通知回调（在锁外调用，避免死锁）
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
    
    LOG_I("Module FSM: Destroyed");
    return 0;
}

const char* module_state_to_str(module_state_t state)
{
    if (state < 0 || state >= MODULE_STATE_MAX) {
        return "UNKNOWN";
    }
    return g_module_state_str[state];
}

const char* module_event_to_str(module_event_t event)
{
    if (event < 0 || event >= MODULE_EVENT_MAX) {
        return "UNKNOWN";
    }
    return g_module_event_str[event];
}

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

static bool _module_fsm_check_trans(module_fsm_context_t *ctx,
                                     module_event_t event,
                                     module_state_t *out_next_state)
{
    if (ctx == NULL || out_next_state == NULL) {
        return false;
    }

    // 遍历迁移表，查找匹配的规则
    for (uint32_t i = 0; i < ctx->trans_table_len; i++) {
        if (ctx->trans_table[i].current_state == ctx->current_state &&
            ctx->trans_table[i].event == event) {
            *out_next_state = ctx->trans_table[i].next_state;
            return true;
        }
    }

    return false;
}