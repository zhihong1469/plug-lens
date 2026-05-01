// src/fsm/global_fsm/inc/global_fsm.h
#ifndef GLOBAL_FSM_H
#define GLOBAL_FSM_H

#include "module_fsm.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【Global FSM 铁律】
// 1. 只做全局状态汇总与决策，不干涉子模块内部业务逻辑
// 2. 只依赖 module_fsm 基类，不依赖具体 Service 或总线
// 3. 所有状态变化通过回调通知上层，为总线接入预留接口
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄（内部实现完全封装）
// ==========================================================================
typedef void* global_fsm_handle_t;

// ==========================================================================
// 2. 全局状态枚举（系统级生命周期）
// ==========================================================================
typedef enum {
    GLOBAL_STATE_INVALID = 0,
    GLOBAL_STATE_INIT,           // 初始化中
    GLOBAL_STATE_READY,          // 系统就绪（所有子模块 READY）
    GLOBAL_STATE_RUNNING,        // 系统运行中（所有子模块 RUNNING）
    GLOBAL_STATE_DEGRADED,       // 系统降级（部分子模块 ERROR）
    GLOBAL_STATE_ERROR,          // 系统错误（关键子模块 ERROR）
    GLOBAL_STATE_SHUTTING_DOWN,  // 关机中
    GLOBAL_STATE_OFF,            // 已关机
    GLOBAL_STATE_MAX
} global_state_t;

// ==========================================================================
// 3. 全局事件枚举（驱动全局状态流转）
// ==========================================================================
typedef enum {
    GLOBAL_EVENT_INVALID = 0,
    GLOBAL_EVENT_MODULE_READY,       // 子模块就绪
    GLOBAL_EVENT_MODULE_RUNNING,      // 子模块运行
    GLOBAL_EVENT_MODULE_ERROR,        // 子模块异常
    GLOBAL_EVENT_MODULE_STOPPED,      // 子模块停止
    GLOBAL_EVENT_SYSTEM_START,        // 系统启动指令
    GLOBAL_EVENT_SYSTEM_STOP,         // 系统停止指令
    GLOBAL_EVENT_SYSTEM_SHUTDOWN,     // 系统关机指令
    GLOBAL_EVENT_MAX
} global_event_t;

// ==========================================================================
// 4. 全局状态变化回调（为 Event Bus 接入预留）
// ==========================================================================
typedef void (*global_state_change_cb_t)(global_state_t old_state,
                                          global_state_t new_state,
                                          void *user_data);

// ==========================================================================
// 5. 全局事件通知回调（为 Event Bus 接入预留）
// ==========================================================================
typedef void (*global_event_notify_cb_t)(global_event_t event,
                                          const char *module_name,
                                          void *user_data);

// ==========================================================================
// 6. Global FSM 配置
// ==========================================================================
typedef struct {
    uint32_t max_modules;               // 最大支持的子模块数量
    global_state_change_cb_t state_cb;  // 全局状态变化回调
    global_event_notify_cb_t event_cb;  // 全局事件通知回调
    void *user_data;                     // 用户数据
} global_fsm_config_t;

// ==========================================================================
// 7. 【核心】Global FSM 对外接口
// ==========================================================================

/**
 * @brief 初始化全局主状态机
 * @param config 配置
 * @param out_handle 输出句柄
 * @return 0成功，非0失败
 */
int global_fsm_init(const global_fsm_config_t *config,
                    global_fsm_handle_t *out_handle);

/**
 * @brief 注册子模块（子模块的 state_cb 会绑定到 Global FSM）
 * @param handle Global FSM 句柄
 * @param module_name 子模块名称（唯一标识）
 * @param module_fsm 子模块状态机句柄
 * @param is_critical 是否为关键模块（关键模块 ERROR 会导致全局 ERROR）
 * @return 0成功，非0失败
 */
int global_fsm_register_module(global_fsm_handle_t handle,
                               const char *module_name,
                               module_fsm_handle_t module_fsm,
                               bool is_critical);

/**
 * @brief 投递全局事件（驱动全局状态流转）
 * @param handle Global FSM 句柄
 * @param event 全局事件
 * @return 0成功，非0失败
 */
int global_fsm_post_event(global_fsm_handle_t handle, global_event_t event);

/**
 * @brief 获取当前全局状态
 * @param handle Global FSM 句柄
 * @return 当前全局状态
 */
global_state_t global_fsm_get_state(global_fsm_handle_t handle);

/**
 * @brief 销毁全局主状态机
 * @param handle Global FSM 句柄
 * @return 0成功，非0失败
 */
int global_fsm_deinit(global_fsm_handle_t handle);

/**
 * @brief 【辅助】全局状态转字符串
 * @param state 状态
 * @return 字符串
 */
const char* global_state_to_str(global_state_t state);

/**
 * @brief 【辅助】全局事件转字符串
 * @param event 事件
 * @return 字符串
 */
const char* global_event_to_str(global_event_t event);

#endif /* GLOBAL_FSM_H */
