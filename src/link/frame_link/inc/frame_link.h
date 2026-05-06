#ifndef FRAME_LINK_H
#define FRAME_LINK_H

#include "video_hal.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【Link层铁律】
// 1. 绝对不修改帧数据、不做AI推理/编码
// 2. 绝对不直接向上层暴露帧池、队列、锁
// 3. 只调用HAL层接口，不依赖更上层代码
// 4. 绝对不创建/管理业务线程
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄（内部实现完全封装）
// ==========================================================================
typedef void* frame_link_handle_t;

// ==========================================================================
// 2. 链路配置
// ==========================================================================
typedef struct {
    video_config_t hal_config;  // HAL层配置（透传）
    uint32_t frame_pool_size;   // 帧池大小（建议5-10）
    uint32_t queue_size;        // 队列大小（建议3-5）
} frame_link_config_t;

// ==========================================================================
// 3. 【核心】Link层对外接口（无线程，纯数据）
// ==========================================================================

/**
 * @brief 初始化视频帧链路
 * @param config 链路配置
 * @param exit_pipe_read_fd 注入全局退出管道
 * @param out_handle 输出：不透明句柄
 * @return 错误码（复用HAL层错误码）
 */
video_err_t frame_link_init(const frame_link_config_t *config,
                            int exit_pipe_read_fd,
                            frame_link_handle_t *out_handle);

/**
 * @brief 启动HAL层流（仅硬件操作，不创建线程）
 * @param handle 句柄
 * @return 错误码
 */
video_err_t frame_link_start_stream(frame_link_handle_t handle);

/**
 * @brief 停止HAL层流（仅硬件操作，不停止线程）
 * @param handle 句柄
 * @return 错误码
 */
video_err_t frame_link_stop_stream(frame_link_handle_t handle);

/**
 * @brief 【Srv层线程用】从链路获取一帧（阻塞，内部处理poll）
 * @param handle 句柄
 * @param frame 输出帧
 * @param timeout_ms 超时时间（毫秒，0表示无限等待）
 * @return 错误码
 * @note 必须调用 frame_link_put_frame() 归还！
 */
video_err_t frame_link_get_frame(frame_link_handle_t handle,
                                  video_frame_t *frame,
                                  uint32_t timeout_ms);

/**
 * @brief 【Srv层线程用】归还帧到链路
 * @param handle 句柄
 * @param frame 帧
 * @return 错误码
 */
video_err_t frame_link_put_frame(frame_link_handle_t handle,
                                  const video_frame_t *frame);

/**
 * @brief 反初始化链路（释放所有资源）
 * @param handle 句柄
 * @return 错误码
 */
video_err_t frame_link_deinit(frame_link_handle_t handle);

#endif /* FRAME_LINK_H */