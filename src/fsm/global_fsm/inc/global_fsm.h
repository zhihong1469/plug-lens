// src/fsm/global_fsm/inc/global_fsm.h
#ifndef GLOBAL_FSM_H
#define GLOBAL_FSM_H

#include "module_fsm.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【全局状态机铁律】
// 1. 系统生命周期唯一管控入口，定义核心状态
// 2. 绝对管控所有子模块状态机，子模块必须服从全局调度
// 3. 异常处理总入口，决策降级/重试/重启/停机策略
// 4. 只做状态决策和调度，不执行具体业务操作
// 5. 所有状态流转有明确规则，杜绝非法跳转
// ==========================================================================

// ==========================================================================
// 1. 系统全局状态枚举（核心生命周期）
// ==========================================================================
typedef enum {
    GLOBAL_STATE_INVALID = 0,
    GLOBAL_STATE_INIT,         // 初始化中
    GLOBAL_STATE_READY,        // 就绪（所有模块初始化完成）
    GLOBAL_STATE_RUNNING,      // 正常运行
    GLOBAL_STATE_DEGRADED,     // 降级运行（部分模块异常）
    GLOBAL_STATE_ERROR,        // 系统异常
    GLOBAL_STATE_SHUTDOWN,     // 关机中
    GLOBAL_STATE_OFF,          // 已关机
    GLOBAL_STATE_MAX
} global_state_t;

// ==========================================================================
// 2. 全局事件枚举（驱动系统生命周期）
// ==========================================================================
typedef enum {
    GLOBAL_EVENT_INVALID = 0,
    GLOBAL_EVENT_INIT_OK,          // 所有模块初始化完成
    GLOBAL_EVENT_SYSTEM_START,     // 系统启动指令
    GLOBAL_EVENT_ALL_MODULE_RUNNING, // 所有模块启动完成
    GLOBAL_EVENT_MODULE_ERROR,     // 子模块异常上报
    GLOBAL_EVENT_MODULE_RECOVERY,  // 子模块恢复
    GLOBAL_EVENT_SYSTEM_STOP,      // 系统停止指令
    GLOBAL_EVENT_ALL_MODULE_STOPPED, // 所有模块停止完成
    GLOBAL_EVENT_SHUTDOWN,         // 系统关机指令
    GLOBAL_EVENT_FATAL_ERROR,      // 致命异常
    GLOBAL_EVENT_MAX
} global_event_t;

// ==========================================================================
// 3. 不透明句柄
// ==========================================================================
typedef void* global_fsm_handle_t;

// ==========================================================================
// 4. 通用回调接口（总线适配）
// ==========================================================================
typedef void (*global_state_change_cb_t)(global_state_t old_state,
                                          global_state_t new_state,
                                          void *user_data);
typedef void (*global_exception_handler_t)(global_event_t event,
                                            const char *module_name,
                                            void *user_data);

// ==========================================================================
// 5. 全局状态机配置
// ==========================================================================
typedef struct {
    uint32_t max_module_count;               // 最大支持子模块数量
    global_state_change_cb_t state_cb;       // 全局状态变更回调
    global_exception_handler_t exception_cb; // 异常处理回调
    void *user_data;
} global_fsm_config_t;

// ==========================================================================
// 6. 核心接口
// ==========================================================================

/**
 * @brief 初始化全局状态机（单例）
 * @param config 配置
 * @param out_handle 输出句柄
 * @return 0成功
 */
int global_fsm_init(const global_fsm_config_t *config, global_fsm_handle_t *out_handle);

/**
 * @brief 注册子模块状态机（全局管控）
 * @param handle 全局句柄
 * @param module_handle 子模块句柄
 * @return 0成功
 */
int global_fsm_register_module(global_fsm_handle_t handle, module_fsm_handle_t module_handle);

/**
 * @brief 注销子模块状态机
 * @param handle 全局句柄
 * @param module_handle 子模块句柄
 * @return 0成功
 */
int global_fsm_unregister_module(global_fsm_handle_t handle, module_fsm_handle_t module_handle);

/**
 * @brief 投递全局事件，驱动系统状态流转
 * @param handle 全局句柄
 * @param event 事件
 * @param module_name 触发事件的模块名（异常事件用）
 * @return 0成功
 */
int global_fsm_post_event(global_fsm_handle_t handle, global_event_t event, const char *module_name);

/**
 * @brief 获取当前全局状态
 * @param handle 全局句柄
 * @return 全局状态
 */
global_state_t global_fsm_get_state(global_fsm_handle_t handle);

/**
 * @brief 全局状态转字符串（可视化用）
 * @param state 状态
 * @return 状态名称
 */
const char* global_state_to_str(global_state_t state);

/**
 * @brief 全局事件转字符串
 * @param event 事件
 * @return 事件名称
 */
const char* global_event_to_str(global_event_t event);

/**
 * @brief 销毁全局状态机
 * @param handle 全局句柄
 * @return 0成功
 */
int global_fsm_deinit(global_fsm_handle_t handle);

#endif /* GLOBAL_FSM_H */
