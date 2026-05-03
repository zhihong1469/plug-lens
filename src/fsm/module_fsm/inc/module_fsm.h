// src/fsm/module_fsm/inc/module_fsm.h
#ifndef MODULE_FSM_H
#define MODULE_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// ==========================================================================
// 【Module FSM 铁律】
// 1. 纯基类，不包含任何具体业务逻辑
// 2. 具体业务逻辑（迁移表、事件处理）由上层通过配置传入
// 3. 状态迁移全程原子性，线程安全
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄（内部实现完全封装）
// ==========================================================================
typedef void* module_fsm_handle_t;

// ==========================================================================
// 2. 通用模块状态枚举（所有子模块共用）
// ==========================================================================
typedef enum {
    MODULE_STATE_INVALID = 0,
    MODULE_STATE_IDLE,           // 空闲（已创建，未初始化）
    MODULE_STATE_INITIALIZING,   // 初始化中
    MODULE_STATE_READY,          // 就绪（等待启动指令）
    MODULE_STATE_STARTING,       // 启动中
    MODULE_STATE_RUNNING,        // 运行中
    MODULE_STATE_PAUSING,        // 暂停中
    MODULE_STATE_PAUSED,         // 已暂停
    MODULE_STATE_STOPPING,       // 停止中
    MODULE_STATE_ERROR,          // 异常
    MODULE_STATE_DEINITIALIZING, // 销毁中
    MODULE_STATE_DEINIT,         // 已销毁
    MODULE_STATE_MAX
} module_state_t;

// ==========================================================================
// 3. 通用模块事件枚举（驱动状态流转）
// ==========================================================================
typedef enum {
    MODULE_EVENT_INVALID = 0,
    MODULE_EVENT_INIT,           // 初始化指令
    MODULE_EVENT_INIT_OK,        // 初始化完成
    MODULE_EVENT_INIT_FAIL,      // 初始化失败
    MODULE_EVENT_START,          // 启动指令
    MODULE_EVENT_START_OK,       // 启动完成
    MODULE_EVENT_START_FAIL,     // 启动失败
    MODULE_EVENT_PAUSE,          // 暂停指令
    MODULE_EVENT_PAUSE_OK,       // 暂停完成
    MODULE_EVENT_RESUME,         // 恢复指令
    MODULE_EVENT_RESUME_OK,      // 恢复完成
    MODULE_EVENT_STOP,           // 停止指令
    MODULE_EVENT_STOP_OK,        // 停止完成
    MODULE_EVENT_ERROR,          // 异常上报
    MODULE_EVENT_ERROR_CLEAR,    // 异常清除
    MODULE_EVENT_DEINIT,         // 销毁指令
    MODULE_EVENT_DEINIT_OK,      // 销毁完成
    MODULE_EVENT_MAX
} module_event_t;

// ==========================================================================
// 4. 状态迁移规则结构体（由具体业务层定义）
// ==========================================================================
typedef struct {
    module_state_t current_state; // 当前状态
    module_event_t event;          // 触发事件
    module_state_t next_state;     // 目标状态
} module_state_trans_t;

// ==========================================================================
// 5. 事件处理回调（由具体业务层实现，执行业务逻辑）
// 
// 返回值：
//   0  - 允许状态迁移
//   非0 - 禁止状态迁移（业务逻辑执行失败）
// ==========================================================================
typedef int (*module_event_handler_t)(module_event_t event, void *user_data);

// ==========================================================================
// 6. 状态变化通知回调（由上层绑定，用于通知 Global FSM 或 Event Bus）
// ==========================================================================
typedef void (*module_state_change_cb_t)(const char *module_name,
                                          module_state_t old_state,
                                          module_state_t new_state,
                                          void *user_data);

// ==========================================================================
// 7. Module FSM 配置结构体
// ==========================================================================
typedef struct {
    const char *module_name;                // 模块名称（唯一标识）
    const module_state_trans_t *trans_table; // 状态迁移表（由业务层提供）
    uint32_t trans_table_len;                // 迁移表长度
    module_event_handler_t event_handler;    // 事件处理回调（业务逻辑）
    module_state_change_cb_t state_cb;       // 状态变化通知回调（上层）
    void *user_data;                          // 用户数据（透传）
} module_fsm_config_t;

// ==========================================================================
// 8. 【核心】通用接口
// ==========================================================================

/**
 * @brief 创建模块状态机
 * @param config 配置（包含业务层提供的迁移表和回调）
 * @param out_handle 输出句柄
 * @return 0成功，非0失败
 */
int module_fsm_create(const module_fsm_config_t *config,
                      module_fsm_handle_t *out_handle);

/**
 * @brief 投递事件（驱动状态流转的唯一入口）
 * @param handle 句柄
 * @param event 事件
 * @return 0成功（允许迁移），非0失败（禁止迁移或参数错误）
 */
int module_fsm_post_event(module_fsm_handle_t handle, module_event_t event);

/**
 * @brief 获取当前状态
 * @param handle 句柄
 * @return 当前状态
 */
module_state_t module_fsm_get_state(module_fsm_handle_t handle);

/**
 * @brief 获取模块名称
 * @param handle 句柄
 * @return 模块名称
 */
const char* module_fsm_get_name(module_fsm_handle_t handle);

/**
 * @brief 销毁模块状态机
 * @param handle 句柄
 * @return 0成功，非0失败
 */
int module_fsm_destroy(module_fsm_handle_t handle);

/**
 * @brief 【辅助】状态转字符串
 * @param state 状态
 * @return 字符串
 */
const char* module_state_to_str(module_state_t state);

/**
 * @brief 【辅助】事件转字符串
 * @param event 事件
 * @return 字符串
 */
const char* module_event_to_str(module_event_t event);

#endif /* MODULE_FSM_H */