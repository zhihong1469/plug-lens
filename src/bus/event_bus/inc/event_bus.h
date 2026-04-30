// src/bus/event_bus/inc/event_bus.h
#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【Event Bus铁律】
// 1. 只做事件路由，不做业务处理
// 2. 不依赖任何业务层，完全独立
// 3. 轻量高效，只传递小数据
// 4. 可监控，支持统计/调试
// ==========================================================================

// ==========================================================================
// 1. 事件优先级（用于调度）
// ==========================================================================
typedef enum {
    EVENT_PRIORITY_LOW = 0,
    EVENT_PRIORITY_NORMAL,
    EVENT_PRIORITY_HIGH,
    EVENT_PRIORITY_CRITICAL,
    EVENT_PRIORITY_MAX
} event_priority_t;

// ==========================================================================
// 2. 通用事件类型（所有模块统一）
// ==========================================================================
typedef enum {
    EVENT_TYPE_INVALID = 0,
    // 系统事件
    EVENT_TYPE_SYSTEM_INIT,
    EVENT_TYPE_SYSTEM_START,
    EVENT_TYPE_SYSTEM_STOP,
    EVENT_TYPE_SYSTEM_SHUTDOWN,
    // 采集事件
    EVENT_TYPE_CAPTURE_START,
    EVENT_TYPE_CAPTURE_STOP,
    EVENT_TYPE_CAPTURE_FRAME_READY,
    // AI事件
    EVENT_TYPE_AI_START,
    EVENT_TYPE_AI_STOP,
    EVENT_TYPE_AI_RESULT_READY,
    // 显示事件
    EVENT_TYPE_DISPLAY_START,
    EVENT_TYPE_DISPLAY_STOP,
    // 存储事件
    EVENT_TYPE_STORAGE_START,
    EVENT_TYPE_STORAGE_STOP,
    // 异常事件
    EVENT_TYPE_ERROR,
    EVENT_TYPE_WARNING,
    // 自定义事件（模块可扩展）
    EVENT_TYPE_CUSTOM_BASE = 0x1000,
    EVENT_TYPE_MAX
} event_type_t;

// ==========================================================================
// 3. 事件结构体（轻量，只传小数据）
// ==========================================================================
typedef struct {
    event_type_t type;
    event_priority_t priority;
    uint64_t timestamp;       // 微秒级时间戳
    const char *source;       // 事件源（模块名）
    void *data;               // 事件数据（小数据，大数据走Data Bus）
    uint32_t data_len;        // 数据长度
} event_t;

// ==========================================================================
// 4. 事件回调函数类型
// ==========================================================================
typedef void (*event_callback_t)(const event_t *event, void *user_data);

// ==========================================================================
// 5. 不透明句柄
// ==========================================================================
typedef void* event_bus_handle_t;

// ==========================================================================
// 6. 总线配置
// ==========================================================================
typedef struct {
    uint32_t max_subscribers;   // 最大订阅者数量
    uint32_t max_event_queue;   // 事件队列大小
    bool enable_stats;          // 是否启用统计
} event_bus_config_t;

// ==========================================================================
// 7. 统计信息（可监控）
// ==========================================================================
typedef struct {
    uint64_t total_published;   // 总发布事件数
    uint64_t total_delivered;    // 总投递事件数
    uint64_t total_dropped;      // 总丢弃事件数
    uint64_t event_count[EVENT_TYPE_MAX]; // 各类型事件计数
} event_bus_stats_t;

// ==========================================================================
// 8. 核心接口
// ==========================================================================

/**
 * @brief 初始化事件总线（单例）
 * @param config 配置
 * @param out_handle 输出句柄
 * @return 0成功
 */
int event_bus_init(const event_bus_config_t *config, event_bus_handle_t *out_handle);

/**
 * @brief 订阅事件
 * @param handle 句柄
 * @param type 事件类型
 * @param cb 回调函数
 * @param user_data 用户数据
 * @return 0成功
 */
int event_bus_subscribe(event_bus_handle_t handle,
                         event_type_t type,
                         event_callback_t cb,
                         void *user_data);

/**
 * @brief 取消订阅
 * @param handle 句柄
 * @param type 事件类型
 * @param cb 回调函数
 * @return 0成功
 */
int event_bus_unsubscribe(event_bus_handle_t handle,
                           event_type_t type,
                           event_callback_t cb);

/**
 * @brief 发布事件（核心入口）
 * @param handle 句柄
 * @param event 事件
 * @return 0成功
 */
int event_bus_publish(event_bus_handle_t handle, const event_t *event);

/**
 * @brief 获取统计信息（可监控）
 * @param handle 句柄
 * @param stats 输出统计
 * @return 0成功
 */
int event_bus_get_stats(event_bus_handle_t handle, event_bus_stats_t *stats);

/**
 * @brief 重置统计信息
 * @param handle 句柄
 * @return 0成功
 */
int event_bus_reset_stats(event_bus_handle_t handle);

/**
 * @brief 销毁事件总线
 * @param handle 句柄
 * @return 0成功
 */
int event_bus_deinit(event_bus_handle_t handle);

/**
 * @brief 事件类型转字符串（调试用）
 * @param type 事件类型
 * @return 事件名称
 */
const char* event_type_to_str(event_type_t type);

/**
 * @brief 优先级转字符串
 * @param priority 优先级
 * @return 优先级名称
 */
const char* event_priority_to_str(event_priority_t priority);

#endif /* EVENT_BUS_H */
