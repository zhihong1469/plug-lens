// src/bus/data_bus/inc/data_bus.h
#ifndef DATA_BUS_H
#define DATA_BUS_H

#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【Data Bus铁律】
// 1. 零拷贝传递，只传指针+元信息
// 2. 不做数据修改/处理
// 3. 不依赖任何业务层，完全独立
// 4. 引用计数管理，自动释放
// 5. 可监控，支持统计/调试
// ==========================================================================

// ==========================================================================
// 1. 数据类型枚举
// ==========================================================================
typedef enum {
    DATA_TYPE_INVALID = 0,
    DATA_TYPE_VIDEO_FRAME,    // 视频帧
    DATA_TYPE_AUDIO_FRAME,    // 音频帧
    DATA_TYPE_AI_RESULT,      // AI推理结果
    DATA_TYPE_IMAGE,          // 图片
    DATA_TYPE_CUSTOM_BASE = 0x1000,
    DATA_TYPE_MAX
} data_type_t;

// ==========================================================================
// 2. 数据帧结构体（零拷贝，只传指针）
// ==========================================================================
typedef struct {
    data_type_t type;
    uint64_t timestamp;       // 微秒级时间戳
    const char *source;       // 数据源（模块名）
    void *data;               // 数据指针（零拷贝，不修改）
    uint32_t data_len;        // 数据长度
    uint32_t ref_count;       // 引用计数（自动管理）
    // 元信息（根据数据类型填充）
    union {
        struct {
            uint32_t width;
            uint32_t height;
            uint32_t format;
        } video;
        struct {
            uint32_t sample_rate;
            uint32_t channels;
            uint32_t bits_per_sample;
        } audio;
        struct {
            uint32_t class_id;
            float confidence;
            float bbox[4];
        } ai;
    } meta;
} data_frame_t;

// ==========================================================================
// 3. 数据释放回调类型（用于自定义释放逻辑）
// ==========================================================================
typedef void (*data_free_callback_t)(void *data, void *user_data);

// ==========================================================================
// 4. 不透明句柄
// ==========================================================================
typedef void* data_bus_handle_t;

// ==========================================================================
// 5. 总线配置
// ==========================================================================
typedef struct {
    uint32_t max_frames;       // 最大帧数量
    bool enable_stats;          // 是否启用统计
} data_bus_config_t;

// ==========================================================================
// 6. 统计信息（可监控）
// ==========================================================================
typedef struct {
    uint64_t total_published;   // 总发布帧数
    uint64_t total_acquired;    // 总获取帧数
    uint64_t total_released;    // 总释放帧数
    uint64_t frame_count[DATA_TYPE_MAX]; // 各类型帧数
} data_bus_stats_t;

// ==========================================================================
// 7. 核心接口
// ==========================================================================

/**
 * @brief 初始化数据总线（单例）
 * @param config 配置
 * @param out_handle 输出句柄
 * @return 0成功
 */
int data_bus_init(const data_bus_config_t *config, data_bus_handle_t *out_handle);

/**
 * @brief 分配数据帧（由生产者调用）
 * @param handle 句柄
 * @param type 数据类型
 * @param data_len 数据长度
 * @param out_frame 输出帧
 * @return 0成功
 */
int data_bus_alloc_frame(data_bus_handle_t handle,
                         data_type_t type,
                         uint32_t data_len,
                         data_frame_t **out_frame);

/**
 * @brief 发布数据帧（生产者调用，引用计数+1）
 * @param handle 句柄
 * @param frame 帧
 * @return 0成功
 */
int data_bus_publish(data_bus_handle_t handle, data_frame_t *frame);

/**
 * @brief 获取数据帧（消费者调用，引用计数+1）
 * @param handle 句柄
 * @param type 数据类型
 * @param out_frame 输出帧
 * @param timeout_ms 超时时间（毫秒，0表示无限等待）
 * @return 0成功
 */
int data_bus_acquire(data_bus_handle_t handle,
                     data_type_t type,
                     data_frame_t **out_frame,
                     uint32_t timeout_ms);

/**
 * @brief 释放数据帧（消费者调用，引用计数-1，为0时自动释放）
 * @param handle 句柄
 * @param frame 帧
 * @return 0成功
 */
int data_bus_release(data_bus_handle_t handle, data_frame_t *frame);

/**
 * @brief 设置自定义释放回调
 * @param handle 句柄
 * @param frame 帧
 * @param cb 释放回调
 * @param user_data 用户数据
 * @return 0成功
 */
int data_bus_set_free_callback(data_bus_handle_t handle,
                                data_frame_t *frame,
                                data_free_callback_t cb,
                                void *user_data);

/**
 * @brief 获取统计信息（可监控）
 * @param handle 句柄
 * @param stats 输出统计
 * @return 0成功
 */
int data_bus_get_stats(data_bus_handle_t handle, data_bus_stats_t *stats);

/**
 * @brief 重置统计信息
 * @param handle 句柄
 * @return 0成功
 */
int data_bus_reset_stats(data_bus_handle_t handle);

/**
 * @brief 销毁数据总线
 * @param handle 句柄
 * @return 0成功
 */
int data_bus_deinit(data_bus_handle_t handle);

/**
 * @brief 数据类型转字符串（调试用）
 * @param type 数据类型
 * @return 数据类型名称
 */
const char* data_type_to_str(data_type_t type);

#endif /* DATA_BUS_H */