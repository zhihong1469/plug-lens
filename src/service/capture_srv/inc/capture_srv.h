// src/service/capture_srv/inc/capture_srv.h
#ifndef CAPTURE_SRV_H
#define CAPTURE_SRV_H

#include "frame_link.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【Service层铁律】
// 1. 绝对不碰硬件驱动，只通过链路层获取数据
// 2. 服务间绝对不直接调用，只通过回调/总线通信
// 3. 不实现具体业务逻辑，只提供原子能力
// 4. 【核心】严格单向依赖：只依赖Link层，不依赖总线层
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄（内部实现完全封装）
// ==========================================================================
typedef void* capture_srv_handle_t;

// ==========================================================================
// 2. 通用事件类型（不绑定总线，独立定义）
// ==========================================================================
typedef enum {
    CAPTURE_SRV_EVENT_INVALID = 0,
    CAPTURE_SRV_EVENT_STARTED,
    CAPTURE_SRV_EVENT_STOPPED,
    CAPTURE_SRV_EVENT_FRAME_READY,
    CAPTURE_SRV_EVENT_ERROR,
} capture_srv_event_type_t;

// ==========================================================================
// 3. 通用事件结构体（不绑定总线）
// ==========================================================================
typedef struct {
    capture_srv_event_type_t type;
    uint64_t timestamp;
    void *data;
    uint32_t data_len;
} capture_srv_event_t;

// ==========================================================================
// 4. 通用回调接口（总线层后续适配）
// ==========================================================================
typedef void (*capture_srv_event_cb_t)(const capture_srv_event_t *event, void *user_data);

// ==========================================================================
// 5. 采集服务配置
// ==========================================================================
typedef struct {
    frame_link_config_t link_config;  // 链路层配置（透传）
    bool auto_start;                   // 是否自动启动
} capture_srv_config_t;

// ==========================================================================
// 6. 【核心】采集服务对外接口
// ==========================================================================

/**
 * @brief 初始化采集服务
 * @param config 服务配置
 * @param out_handle 输出：不透明句柄
 * @return 错误码（0表示成功）
 */
int capture_srv_init(const capture_srv_config_t *config,
                     capture_srv_handle_t *out_handle);

/**
 * @brief 启动采集（原子能力）
 * @param handle 句柄
 * @return 错误码
 */
int capture_srv_start(capture_srv_handle_t handle);

/**
 * @brief 停止采集（原子能力）
 * @param handle 句柄
 * @return 错误码
 */
int capture_srv_stop(capture_srv_handle_t handle);

/**
 * @brief 从服务获取一帧（原子能力）
 * @param handle 句柄
 * @param frame 输出帧
 * @param timeout_ms 超时时间（毫秒，0表示无限等待）
 * @return 错误码
 * @note 必须调用 capture_srv_put_frame() 归还！
 */
int capture_srv_get_frame(capture_srv_handle_t handle,
                          video_frame_t *frame,
                          uint32_t timeout_ms);

/**
 * @brief 归还帧
 * @param handle 句柄
 * @param frame 帧
 * @return 错误码
 */
int capture_srv_put_frame(capture_srv_handle_t handle,
                          const video_frame_t *frame);

/**
 * @brief 注册事件回调（总线层后续通过此接口适配）
 * @param handle 句柄
 * @param cb 回调函数
 * @param user_data 用户数据
 * @return 错误码
 */
int capture_srv_register_event_cb(capture_srv_handle_t handle,
                                    capture_srv_event_cb_t cb,
                                    void *user_data);

/**
 * @brief 反初始化采集服务
 * @param handle 句柄
 * @return 错误码
 */
int capture_srv_deinit(capture_srv_handle_t handle);

#endif /* CAPTURE_SRV_H */