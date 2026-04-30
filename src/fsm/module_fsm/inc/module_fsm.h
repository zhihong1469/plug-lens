// src/fsm/module_fsm/inc/module_fsm.h
#ifndef MODULE_FSM_H
#define MODULE_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// ==========================================================================
// 【模块状态机铁律】
// 1. 通用状态抽象到基类，子模块仅实现差异化规则
// 2. 必须服从全局状态机调度，禁止私自非法跳转
// 3. 只做状态决策，不执行具体业务操作
// 4. 状态迁移必须原子性，全程加锁保护
// ==========================================================================

// ==========================================================================
// 1. 模块通用状态枚举（所有子模块统一）
// ==========================================================================
typedef enum {
    MODULE_STATE_INVALID = 0,    // 无效状态
    MODULE_STATE_IDLE,           // 空闲（已初始化，未启动）
    MODULE_STATE_READY,          // 就绪（等待全局调度启动）
    MODULE_STATE_RUNNING,        // 运行中
    MODULE_STATE_PAUSED,         // 暂停
    MODULE_STATE_ERROR,          // 异常
    MODULE_STATE_DEINIT,         // 已释放
    MODULE_STATE_MAX
} module_state_t;

// ==========================================================================
// 2. 模块事件枚举（驱动状态流转）
// ==========================================================================
typedef enum {
    MODULE_EVENT_INVALID = 0,
    MODULE_EVENT_INIT_OK,        // 初始化完成
    MODULE_EVENT_START,          // 启动指令（来自全局状态机）
    MODULE_EVENT_START_OK,       // 启动成功
    MODULE_EVENT_PAUSE,          // 暂停指令
    MODULE_EVENT_PAUSE_OK,       // 暂停成功
    MODULE_EVENT_RESUME,         // 恢复指令
    MODULE_EVENT_RESUME_OK,      // 恢复成功
    MODULE_EVENT_STOP,           // 停止指令
    MODULE_EVENT_STOP_OK,        // 停止成功
    MODULE_EVENT_ERROR,          // 异常上报
    MODULE_EVENT_DEINIT,         // 释放指令
    MODULE_EVENT_MAX
} module_event_t;

// ==========================================================================
// 3. 状态迁移规则结构体（子模块自定义）
// ==========================================================================
typedef struct {
    module_state_t current_state;    // 当前状态
    module_event_t event;            // 触发事件
    module_state_t next_state;       // 目标状态
} module_state_trans_t;

// ==========================================================================
// 4. 通用回调接口（总线适配用，不绑定具体实现）
// ==========================================================================
// 状态变更回调：通知总线/全局状态机
typedef void (*module_state_change_cb_t)(const char *module_name,
                                          module_state_t old_state,
                                          module_state_t new_state,
                                          void *user_data);
// 事件执行回调：子模块自定义事件处理逻辑（仅决策，不执行业务）
typedef int (*module_event_handler_t)(module_event_t event, void *user_data);

// ==========================================================================
// 5. 模块状态机基类（不透明句柄）
// ==========================================================================
typedef void* module_fsm_handle_t;

// ==========================================================================
// 6. 模块状态机配置
// ==========================================================================
typedef struct {
    const char *module_name;                // 模块名称（唯一标识）
    module_state_trans_t *trans_table;      // 状态迁移表
    uint32_t trans_table_len;               // 迁移表长度
    module_state_change_cb_t state_cb;      // 状态变更回调
    module_event_handler_t event_handler;   // 事件处理回调
    void *user_data;                        // 用户数据
} module_fsm_config_t;

// ==========================================================================
// 7. 通用接口
// ==========================================================================

/**
 * @brief 创建模块状态机
 * @param config 配置
 * @param out_handle 输出句柄
 * @return 0成功，非0失败
 */
int module_fsm_create(const module_fsm_config_t *config, module_fsm_handle_t *out_handle);

/**
 * @brief 销毁模块状态机
 * @param handle 句柄
 * @return 0成功
 */
int module_fsm_destroy(module_fsm_handle_t handle);

/**
 * @brief 投递事件，驱动状态流转（核心入口，仅总线/全局状态机可调用）
 * @param handle 句柄
 * @param event 事件
 * @return 0成功，非0非法跳转
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
 * @brief 状态转字符串（可视化调试用）
 * @param state 状态
 * @return 状态名称字符串
 */
const char* module_state_to_str(module_state_t state);

/**
 * @brief 事件转字符串（可视化调试用）
 * @param event 事件
 * @return 事件名称字符串
 */
const char* module_event_to_str(module_event_t event);

#endif /* MODULE_FSM_H */
