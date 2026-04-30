// src/fsm/global_fsm/src/global_fsm.c
#include "global_fsm.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ==========================================================================
// 内部上下文结构体
// ==========================================================================
#define MAX_MODULE_COUNT 32
typedef struct {
    global_state_t current_state;
    module_fsm_handle_t modules[MAX_MODULE_COUNT];
    uint32_t module_count;
    uint32_t max_module_count;
    global_state_change_cb_t state_cb;
    global_exception_handler_t exception_cb;
    void *user_data;
    pthread_mutex_t lock; // 全局状态锁
} global_fsm_context_t;

// ==========================================================================
// 状态/事件字符串映射表
// ==========================================================================
static const char* g_global_state_str[] = {
    [GLOBAL_STATE_INVALID] = "INVALID",
    [GLOBAL_STATE_INIT] = "INIT",
    [GLOBAL_STATE_READY] = "READY",
    [GLOBAL_STATE_RUNNING] = "RUNNING",
    [GLOBAL_STATE_DEGRADED] = "DEGRADED",
    [GLOBAL_STATE_ERROR] = "ERROR",
    [GLOBAL_STATE_SHUTDOWN] = "SHUTDOWN",
    [GLOBAL_STATE_OFF] = "OFF",
};

static const char* g_global_event_str[] = {
    [GLOBAL_EVENT_INVALID] = "INVALID",
    [GLOBAL_EVENT_INIT_OK] = "INIT_OK",
    [GLOBAL_EVENT_SYSTEM_START] = "SYSTEM_START",
    [GLOBAL_EVENT_ALL_MODULE_RUNNING] = "ALL_MODULE_RUNNING",
    [GLOBAL_EVENT_MODULE_ERROR] = "MODULE_ERROR",
    [GLOBAL_EVENT_MODULE_RECOVERY] = "MODULE_RECOVERY",
    [GLOBAL_EVENT_SYSTEM_STOP] = "SYSTEM_STOP",
    [GLOBAL_EVENT_ALL_MODULE_STOPPED] = "ALL_MODULE_STOPPED",
    [GLOBAL_EVENT_SHUTDOWN] = "SHUTDOWN",
    [GLOBAL_EVENT_FATAL_ERROR] = "FATAL_ERROR",
};

// ==========================================================================
// 全局状态迁移规则表（内置，禁止私自修改）
// ==========================================================================
typedef struct {
    global_state_t current_state;
    global_event_t event;
    global_state_t next_state;
} global_state_trans_t;

static const global_state_trans_t g_global_trans_table[] = {
    // 初始化流程
    {GLOBAL_STATE_INIT, GLOBAL_EVENT_INIT_OK, GLOBAL_STATE_READY},
    // 启动流程
    {GLOBAL_STATE_READY, GLOBAL_EVENT_SYSTEM_START, GLOBAL_STATE_RUNNING},
    {GLOBAL_STATE_RUNNING, GLOBAL_EVENT_ALL_MODULE_RUNNING, GLOBAL_STATE_RUNNING},
    // 异常降级流程
    {GLOBAL_STATE_RUNNING, GLOBAL_EVENT_MODULE_ERROR, GLOBAL_STATE_DEGRADED},
    {GLOBAL_STATE_DEGRADED, GLOBAL_EVENT_MODULE_RECOVERY, GLOBAL_STATE_RUNNING},
    {GLOBAL_STATE_DEGRADED, GLOBAL_EVENT_MODULE_ERROR, GLOBAL_STATE_ERROR},
    {GLOBAL_STATE_RUNNING, GLOBAL_EVENT_FATAL_ERROR, GLOBAL_STATE_ERROR},
    {GLOBAL_STATE_DEGRADED, GLOBAL_EVENT_FATAL_ERROR, GLOBAL_STATE_ERROR},
    // 停止流程
    {GLOBAL_STATE_RUNNING, GLOBAL_EVENT_SYSTEM_STOP, GLOBAL_STATE_READY},
    {GLOBAL_STATE_DEGRADED, GLOBAL_EVENT_SYSTEM_STOP, GLOBAL_STATE_READY},
    {GLOBAL_STATE_READY, GLOBAL_EVENT_ALL_MODULE_STOPPED, GLOBAL_STATE_READY},
    // 关机流程
    {GLOBAL_STATE_READY, GLOBAL_EVENT_SHUTDOWN, GLOBAL_STATE_SHUTDOWN},
    {GLOBAL_STATE_ERROR, GLOBAL_EVENT_SHUTDOWN, GLOBAL_STATE_SHUTDOWN},
    {GLOBAL_STATE_SHUTDOWN, GLOBAL_EVENT_ALL_MODULE_STOPPED, GLOBAL_STATE_OFF},
};
static const uint32_t g_global_trans_len = sizeof(g_global_trans_table) / sizeof(g_global_trans_table[0]);

// ==========================================================================
// 内部辅助函数
// ==========================================================================
static bool _global_fsm_check_trans(global_fsm_context_t *ctx,
                                    global_event_t event,
                                    global_state_t *out_next_state)
{
    for (uint32_t i = 0; i < g_global_trans_len; i++) {
        if (g_global_trans_table[i].current_state == ctx->current_state
            && g_global_trans_table[i].event == event) {
            *out_next_state = g_global_trans_table[i].next_state;
            return true;
        }
    }
    return false;
}

// 检查所有模块是否处于指定状态
static bool _global_fsm_check_all_module_state(global_fsm_context_t *ctx, module_state_t target_state)
{
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (module_fsm_get_state(ctx->modules[i]) != target_state) {
            return false;
        }
    }
    return true;
}

// ==========================================================================
// 对外API实现
// ==========================================================================

const char* global_state_to_str(global_state_t state)
{
    if (state < 0 || state >= GLOBAL_STATE_MAX) {
        return g_global_state_str[GLOBAL_STATE_INVALID];
    }
    return g_global_state_str[state];
}

const char* global_event_to_str(global_event_t event)
{
    if (event < 0 || event >= GLOBAL_EVENT_MAX) {
        return g_global_event_str[GLOBAL_EVENT_INVALID];
    }
    return g_global_event_str[event];
}

int global_fsm_init(const global_fsm_config_t *config, global_fsm_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    global_fsm_context_t *ctx = (global_fsm_context_t*)malloc(sizeof(global_fsm_context_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(global_fsm_context_t));

    // 拷贝配置
    ctx->max_module_count = config->max_module_count > 0 ? config->max_module_count : MAX_MODULE_COUNT;
    ctx->state_cb = config->state_cb;
    ctx->exception_cb = config->exception_cb;
    ctx->user_data = config->user_data;
    ctx->current_state = GLOBAL_STATE_INIT; // 初始状态
    ctx->module_count = 0;

    pthread_mutex_init(&ctx->lock, NULL);

    *out_handle = (global_fsm_handle_t)ctx;
    return 0;
}

int global_fsm_register_module(global_fsm_handle_t handle, module_fsm_handle_t module_handle)
{
    if (handle == NULL || module_handle == NULL) {
        return -1;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    if (ctx->module_count >= ctx->max_module_count) {
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    ctx->modules[ctx->module_count++] = module_handle;
    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

int global_fsm_unregister_module(global_fsm_handle_t handle, module_fsm_handle_t module_handle)
{
    if (handle == NULL || module_handle == NULL) {
        return -1;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (ctx->modules[i] == module_handle) {
            // 前移覆盖
            for (uint32_t j = i; j < ctx->module_count - 1; j++) {
                ctx->modules[j] = ctx->modules[j + 1];
            }
            ctx->module_count--;
            break;
        }
    }
    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

int global_fsm_post_event(global_fsm_handle_t handle, global_event_t event, const char *module_name)
{
    if (handle == NULL || event == GLOBAL_EVENT_INVALID) {
        return -1;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    // 1. 异常事件处理（总入口）
    if (event == GLOBAL_EVENT_MODULE_ERROR || event == GLOBAL_EVENT_FATAL_ERROR) {
        if (ctx->exception_cb != NULL) {
            ctx->exception_cb(event, module_name, ctx->user_data);
        }
    }

    // 2. 校验状态迁移规则
    global_state_t next_state = GLOBAL_STATE_INVALID;
    if (!_global_fsm_check_trans(ctx, event, &next_state)) {
        pthread_mutex_unlock(&ctx->lock);
        return -1; // 非法跳转
    }

    // 3. 执行状态变更
    global_state_t old_state = ctx->current_state;
    ctx->current_state = next_state;
    pthread_mutex_unlock(&ctx->lock);

    // 4. 通知状态变更（总线适配）
    if (ctx->state_cb != NULL) {
        ctx->state_cb(old_state, next_state, ctx->user_data);
    }

    return 0;
}

global_state_t global_fsm_get_state(global_fsm_handle_t handle)
{
    if (handle == NULL) {
        return GLOBAL_STATE_INVALID;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    global_state_t state = ctx->current_state;
    pthread_mutex_unlock(&ctx->lock);

    return state;
}

int global_fsm_deinit(global_fsm_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    ctx->current_state = GLOBAL_STATE_OFF;
    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    return 0;
}
