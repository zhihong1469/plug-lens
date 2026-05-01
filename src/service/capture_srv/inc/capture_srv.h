// src/service/capture_srv/inc/capture_srv.h
#ifndef CAPTURE_SRV_H
#define CAPTURE_SRV_H

#include "frame_link.h"
#include "module_fsm.h" // 只依赖基类
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【Service层铁律】
// 1. 只依赖 Link层 和 module_fsm 基类
// 2. 不依赖 Event Bus 或 Global FSM
// 3. 所有对外通知通过回调完成
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄
// ==========================================================================
typedef void* capture_srv_handle_t;

// ==========================================================================
// 2. 配置
// ==========================================================================
typedef struct {
    frame_link_config_t link_config;
    bool auto_start;
} capture_srv_config_t;

// ==========================================================================
// 3. 【核心】接口
// ==========================================================================

/**
 * @brief 初始化采集服务
 * @param config 配置
 * @param fsm_state_cb 状态变化回调（传给 Global FSM）
 * @param fsm_user_data 用户数据
 * @param out_handle 输出句柄
 * @return 0成功
 */
int capture_srv_init(const capture_srv_config_t *config,
                     module_state_change_cb_t fsm_state_cb,
                     void *fsm_user_data,
                     capture_srv_handle_t *out_handle);

/**
 * @brief 获取子状态机句柄（供 Global FSM 投递事件）
 * @param handle 服务句柄
 * @return 子状态机句柄
 */
module_fsm_handle_t capture_srv_get_fsm(capture_srv_handle_t handle);

/**
 * @brief 获取帧（仅在 RUNNING 状态有效）
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
 * @brief 反初始化
 */
int capture_srv_deinit(capture_srv_handle_t handle);

#endif /* CAPTURE_SRV_H */