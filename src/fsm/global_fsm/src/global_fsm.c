// src/fsm/global_fsm/src/global_fsm.c
#include "global_fsm.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define GLOBAL_FSM_MAX_MODULES_DEFAULT 16

// ==========================================================================
// 内部子模块信息结构体
// ==========================================================================
typedef struct {
    char name[32];                  // 子模块名称
    module_fsm_handle_t fsm;        // 子模块状态机句柄
    module_state_t current_state;   // 子模块当前状态（缓存）
    bool is_critical;               // 是否为关键模块
    bool registered;                // 是否已注册
} module_info_t;

// ==========================================================================
// 内部上下文结构体
// ==========================================================================
typedef struct {
    global_fsm_config_t config;
    global_state_t current_state;
    module_info_t *modules;
    uint32_t module_count;
    uint32_t max_modules;
    pthread_mutex_t lock;
} global_fsm_context_t;

// ==========================================================================
// 字符串映射表
// ==========================================================================
static const char* g_global_state_str[] = {
    [GLOBAL_STATE_INVALID] = "INVALID",
    [GLOBAL_STATE_INIT] = "INIT",
    [GLOBAL_STATE_READY] = "READY",
    [GLOBAL_STATE_RUNNING] = "RUNNING",
    [GLOBAL_STATE_DEGRADED] = "DEGRADED",
    [GLOBAL_STATE_ERROR] = "ERROR",
    [GLOBAL_STATE_SHUTTING_DOWN] = "SHUTTING_DOWN",
    [GLOBAL_STATE_OFF] = "OFF",
};

static const char* g_global_event_str[] = {
    [GLOBAL_EVENT_INVALID] = "INVALID",
    [GLOBAL_EVENT_MODULE_READY] = "MODULE_READY",
    [GLOBAL_EVENT_MODULE_RUNNING] = "MODULE_RUNNING",
    [GLOBAL_EVENT_MODULE_ERROR] = "MODULE_ERROR",
    [GLOBAL_EVENT_MODULE_STOPPED] = "MODULE_STOPPED",
    [GLOBAL_EVENT_SYSTEM_START] = "SYSTEM_START",
    [GLOBAL_EVENT_SYSTEM_STOP] = "SYSTEM_STOP",
    [GLOBAL_EVENT_SYSTEM_SHUTDOWN] = "SYSTEM_SHUTDOWN",
};

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static void _global_fsm_update_global_state(global_fsm_context_t *ctx);
static void _global_fsm_notify_event(global_fsm_context_t *ctx,
                                      global_event_t event,
                                      const char *module_name);
static void _global_fsm_change_state(global_fsm_context_t *ctx,
                                      global_state_t new_state);

// ==========================================================================
// 对外API实现
// ==========================================================================

int global_fsm_init(const global_fsm_config_t *config,
                    global_fsm_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    // 分配上下文
    global_fsm_context_t *ctx = (global_fsm_context_t*)malloc(sizeof(global_fsm_context_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(global_fsm_context_t));

    // 拷贝配置
    memcpy(&ctx->config, config, sizeof(global_fsm_config_t));
    ctx->max_modules = (config->max_modules > 0) ? config->max_modules : GLOBAL_FSM_MAX_MODULES_DEFAULT;

    // 分配子模块数组
    ctx->modules = (module_info_t*)malloc(ctx->max_modules * sizeof(module_info_t));
    if (ctx->modules == NULL) {
        free(ctx);
        return -1;
    }
    memset(ctx->modules, 0, ctx->max_modules * sizeof(module_info_t));

    // 初始化锁
    pthread_mutex_init(&ctx->lock, NULL);

    // 初始状态
    ctx->current_state = GLOBAL_STATE_INIT;
    ctx->module_count = 0;

    *out_handle = (global_fsm_handle_t)ctx;
    LOG_I("Global FSM: Initialized, max modules=%u", ctx->max_modules);
    return 0;
}

int global_fsm_register_module(global_fsm_handle_t handle,
                               const char *module_name,
                               module_fsm_handle_t module_fsm,
                               bool is_critical)
{
    if (handle == NULL || module_name == NULL || module_fsm == NULL) {
        return -1;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    // 检查是否已满
    if (ctx->module_count >= ctx->max_modules) {
        LOG_E("Global FSM: Max modules reached");
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    // 检查是否重名
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (strcmp(ctx->modules[i].name, module_name) == 0) {
            LOG_E("Global FSM: Module %s already registered", module_name);
            pthread_mutex_unlock(&ctx->lock);
            return -1;
        }
    }

    // 注册子模块
    module_info_t *info = &ctx->modules[ctx->module_count];
    strncpy(info->name, module_name, sizeof(info->name) - 1);
    info->fsm = module_fsm;
    info->current_state = module_fsm_get_state(module_fsm);
    info->is_critical = is_critical;
    info->registered = true;
    ctx->module_count++;

    // 【关键】这里我们暂时不修改子模块的 state_cb
    // 上层（main.c）应该在创建子模块时，把 Global FSM 的内部回调设为子模块的 state_cb
    // 这样就形成了：子模块 state change -> Global FSM 内部回调 -> 全局决策

    LOG_I("Global FSM: Module %s registered (critical=%d)", module_name, is_critical);
    
    // 注册后立即更新一次全局状态
    pthread_mutex_unlock(&ctx->lock);
    _global_fsm_update_global_state(ctx);
    
    return 0;
}

int global_fsm_post_event(global_fsm_handle_t handle, global_event_t event)
{
    if (handle == NULL || event == GLOBAL_EVENT_INVALID || event >= GLOBAL_EVENT_MAX) {
        return -1;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    LOG_I("Global FSM: Received global event %s", global_event_to_str(event));

    pthread_mutex_lock(&ctx->lock);
    global_state_t current = ctx->current_state;
    pthread_mutex_unlock(&ctx->lock);

    // 根据当前状态和事件，执行动作
    switch (event) {
        case GLOBAL_EVENT_SYSTEM_START: {
            if (current != GLOBAL_STATE_READY) {
                LOG_W("Global FSM: Cannot start, current state %s", global_state_to_str(current));
                return -1;
            }
            // 给所有子模块投 START 事件
            pthread_mutex_lock(&ctx->lock);
            for (uint32_t i = 0; i < ctx->module_count; i++) {
                if (ctx->modules[i].registered) {
                    LOG_I("Global FSM: Sending START to module %s", ctx->modules[i].name);
                    module_fsm_post_event(ctx->modules[i].fsm, MODULE_EVENT_START);
                }
            }
            pthread_mutex_unlock(&ctx->lock);
            break;
        }
        
        case GLOBAL_EVENT_SYSTEM_STOP: {
            if (current != GLOBAL_STATE_RUNNING && current != GLOBAL_STATE_DEGRADED) {
                LOG_W("Global FSM: Cannot stop, current state %s", global_state_to_str(current));
                return -1;
            }
            // 给所有子模块投 STOP 事件
            pthread_mutex_lock(&ctx->lock);
            for (uint32_t i = 0; i < ctx->module_count; i++) {
                if (ctx->modules[i].registered) {
                    LOG_I("Global FSM: Sending STOP to module %s", ctx->modules[i].name);
                    module_fsm_post_event(ctx->modules[i].fsm, MODULE_EVENT_STOP);
                }
            }
            pthread_mutex_unlock(&ctx->lock);
            break;
        }
        
        case GLOBAL_EVENT_SYSTEM_SHUTDOWN: {
            _global_fsm_change_state(ctx, GLOBAL_STATE_SHUTTING_DOWN);
            // 给所有子模块投 DEINIT 事件
            pthread_mutex_lock(&ctx->lock);
            for (uint32_t i = 0; i < ctx->module_count; i++) {
                if (ctx->modules[i].registered) {
                    LOG_I("Global FSM: Sending DEINIT to module %s", ctx->modules[i].name);
                    module_fsm_post_event(ctx->modules[i].fsm, MODULE_EVENT_DEINIT);
                }
            }
            pthread_mutex_unlock(&ctx->lock);
            // 延迟一会后设为 OFF（实际项目中应等所有子模块 DEINIT 回调）
            _global_fsm_change_state(ctx, GLOBAL_STATE_OFF);
            break;
        }
        
        default:
            break;
    }

    // 通知事件（为 Event Bus 预留）
    _global_fsm_notify_event(ctx, event, NULL);
    
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
    
    // 释放子模块数组
    free(ctx->modules);
    ctx->modules = NULL;
    ctx->module_count = 0;
    
    pthread_mutex_unlock(&ctx->lock);
    
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    
    LOG_I("Global FSM: Deinitialized");
    return 0;
}

const char* global_state_to_str(global_state_t state)
{
    if (state < 0 || state >= GLOBAL_STATE_MAX) {
        return "UNKNOWN";
    }
    return g_global_state_str[state];
}

const char* global_event_to_str(global_event_t event)
{
    if (event < 0 || event >= GLOBAL_EVENT_MAX) {
        return "UNKNOWN";
    }
    return g_global_event_str[event];
}

// ==========================================================================
// 【核心】内部辅助函数实现
// ==========================================================================

/**
 * @brief 【关键】子模块状态变化回调（由上层绑定到子模块的 state_cb）
 * 
 * 这是子模块与 Global FSM 交互的核心桥梁：
 * 子模块 state change -> 此回调 -> 更新缓存 -> 重新计算全局状态
 */
void global_fsm_on_module_state_change(const char *module_name,
                                        module_state_t old_state,
                                        module_state_t new_state,
                                        void *user_data)
{
    global_fsm_context_t *ctx = (global_fsm_context_t*)user_data;
    if (ctx == NULL || module_name == NULL) return;

    LOG_I("Global FSM: Module %s state changed: %s -> %s", 
          module_name, 
          module_state_to_str(old_state), 
          module_state_to_str(new_state));

    pthread_mutex_lock(&ctx->lock);

    // 找到对应的子模块，更新缓存状态
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (strcmp(ctx->modules[i].name, module_name) == 0) {
            ctx->modules[i].current_state = new_state;
            break;
        }
    }

    pthread_mutex_unlock(&ctx->lock);

    // 【核心】重新计算全局状态
    _global_fsm_update_global_state(ctx);

    // 【为 Event Bus 预留】根据子模块的新状态，生成对应的全局事件
    global_event_t evt = GLOBAL_EVENT_INVALID;
    switch (new_state) {
        case MODULE_STATE_READY:   evt = GLOBAL_EVENT_MODULE_READY; break;
        case MODULE_STATE_RUNNING: evt = GLOBAL_EVENT_MODULE_RUNNING; break;
        case MODULE_STATE_ERROR:   evt = GLOBAL_EVENT_MODULE_ERROR; break;
        case MODULE_STATE_IDLE:    evt = GLOBAL_EVENT_MODULE_STOPPED; break;
        default: break;
    }
    if (evt != GLOBAL_EVENT_INVALID) {
        _global_fsm_notify_event(ctx, evt, module_name);
    }
}

/**
 * @brief 【核心】全局状态决策算法
 * 
 * 遍历所有子模块的缓存状态，根据规则计算当前全局状态：
 * 1. 有关键模块 ERROR -> GLOBAL_STATE_ERROR
 * 2. 有非关键模块 ERROR -> GLOBAL_STATE_DEGRADED
 * 3. 所有子模块 RUNNING -> GLOBAL_STATE_RUNNING
 * 4. 所有子模块 READY -> GLOBAL_STATE_READY
 */
static void _global_fsm_update_global_state(global_fsm_context_t *ctx)
{
    if (ctx == NULL) return;

    pthread_mutex_lock(&ctx->lock);

    bool all_ready = true;
    bool all_running = true;
    bool has_non_critical_error = false;
    bool has_critical_error = false;

    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (!ctx->modules[i].registered) continue;

        module_state_t state = ctx->modules[i].current_state;
        
        if (state == MODULE_STATE_ERROR) {
            if (ctx->modules[i].is_critical) {
                has_critical_error = true;
            } else {
                has_non_critical_error = true;
            }
        }
        
        if (state != MODULE_STATE_READY) all_ready = false;
        if (state != MODULE_STATE_RUNNING) all_running = false;
    }

    global_state_t new_state = ctx->current_state;

    // 决策逻辑
    if (has_critical_error) {
        new_state = GLOBAL_STATE_ERROR;
    } else if (has_non_critical_error) {
        new_state = GLOBAL_STATE_DEGRADED;
    } else if (all_running) {
        new_state = GLOBAL_STATE_RUNNING;
    } else if (all_ready) {
        new_state = GLOBAL_STATE_READY;
    }
    // 其他情况保持原状态

    pthread_mutex_unlock(&ctx->lock);

    // 执行状态变化
    if (new_state != ctx->current_state) {
        _global_fsm_change_state(ctx, new_state);
    }
}

static void _global_fsm_notify_event(global_fsm_context_t *ctx,
                                      global_event_t event,
                                      const char *module_name)
{
    if (ctx == NULL) return;

    // 【为 Event Bus 预留】调用上层传入的 event_cb
    // 上层（main.c）可以在这里把 global_event 转换成 event_bus 的 event 发布出去
    if (ctx->config.event_cb != NULL) {
        ctx->config.event_cb(event, module_name, ctx->config.user_data);
    }
}

static void _global_fsm_change_state(global_fsm_context_t *ctx,
                                      global_state_t new_state)
{
    if (ctx == NULL) return;

    pthread_mutex_lock(&ctx->lock);
    global_state_t old_state = ctx->current_state;
    
    if (old_state == new_state) {
        pthread_mutex_unlock(&ctx->lock);
        return;
    }

    ctx->current_state = new_state;
    LOG_I("Global FSM: Global state changed: %s -> %s", 
          global_state_to_str(old_state), 
          global_state_to_str(new_state));
    pthread_mutex_unlock(&ctx->lock);

    // 【为 Event Bus 预留】调用上层传入的 state_cb
    if (ctx->config.state_cb != NULL) {
        ctx->config.state_cb(old_state, new_state, ctx->config.user_data);
    }
}