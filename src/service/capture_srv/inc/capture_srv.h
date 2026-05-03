// src/service/capture_srv/inc/capture_srv.h
#ifndef CAPTURE_SRV_H
#define CAPTURE_SRV_H

#include "frame_link.h"
#include "module_fsm.h"
#include "event_bus.h"
#include "data_bus.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【Capture Service 铁律】
// 1. 只依赖 Link层、Module FSM基类、双总线基类
// 2. 所有外部依赖通过 config 注入（无全局变量）
// 3. 所有硬件动作由 FSM 状态迁移触发
// 4. 所有数据通过 Data Bus 流出，所有通知通过 Event Bus 发出
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄
// ==========================================================================
typedef void* capture_srv_handle_t;

// ==========================================================================
// 2. 上层回调接口（由 Global FSM 或 App 实现）
// ==========================================================================
typedef struct {
    module_state_change_cb_t state_change_cb; // 状态变化通知给上层
    void *user_data;
} capture_srv_callbacks_t;

// ==========================================================================
// 3. 完整配置结构体（依赖注入容器）
// ==========================================================================
typedef struct {
    // Link层 配置
    frame_link_config_t link_cfg;
    
    // 双总线 句柄注入
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;
    
    // 上层回调注入
    capture_srv_callbacks_t callbacks;
    
    // 运行时配置
    bool auto_start;
} capture_srv_config_t;

// ==========================================================================
// 4. 【核心】标准 Service 接口
// ==========================================================================

/**
 * @brief 创建并初始化采集服务
 * @param config 配置（包含所有依赖注入）
 * @param out_handle 输出句柄
 * @return 0成功
 */
int capture_srv_create(const capture_srv_config_t *config,
                       capture_srv_handle_t *out_handle);

/**
 * @brief 获取子状态机句柄（供上层投递事件）
 * @param handle 服务句柄
 * @return 子状态机句柄
 */
module_fsm_handle_t capture_srv_get_fsm(capture_srv_handle_t handle);

/**
 * @brief 【可选】直接获取帧（绕过总线，用于低延迟场景）
 * @note 推荐通过 Event/Data Bus 获取
 */
int capture_srv_get_frame(capture_srv_handle_t handle,
                          video_frame_t *frame,
                          uint32_t timeout_ms);

/**
 * @brief 归还帧
 */
int capture_srv_put_frame(capture_srv_handle_t handle,
                          const video_frame_t *frame);

/**
 * @brief 销毁服务
 * @param handle 服务句柄
 * @return 0成功
 */
int capture_srv_destroy(capture_srv_handle_t handle);

#endif /* CAPTURE_SRV_H */