// src/bus/event_bus/inc/event_bus.h
#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// ==========================================================================
// 【Event Bus 铁律】
// 1. 纯发布-订阅，不做任何业务逻辑判断
// 2. 线程安全，支持多生产者多消费者
// 3. 事件回调在锁外执行，避免死锁
// 4. 支持优先级（预留）
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄
// ==========================================================================
typedef void* event_bus_handle_t;

// ==========================================================================
// 2. 事件优先级（预留，当前默认使用 NORMAL）
// ==========================================================================
typedef enum {
    EVENT_PRIORITY_LOW = 0,
    EVENT_PRIORITY_NORMAL,
    EVENT_PRIORITY_HIGH,
    EVENT_PRIORITY_CRITICAL,
    EVENT_PRIORITY_MAX
} event_priority_t;

// ==========================================================================
// 3. 通用事件类型定义（分层预留，方便扩展）
// ==========================================================================
typedef enum {
    EVENT_TYPE_INVALID = 0,

    // 系统层事件 (0x0001 - 0x0FFF)
    EVENT_TYPE_SYS_BASE = 0x0001,
    EVENT_TYPE_SYS_STATE_CHANGED,
    EVENT_TYPE_SYS_START,
    EVENT_TYPE_SYS_STOP,
    EVENT_TYPE_SYS_SHUTDOWN,
    EVENT_TYPE_SYS_ERROR,

    // 模块层事件 (0x1000 - 0x1FFF)
    EVENT_TYPE_MOD_BASE = 0x1000,
    EVENT_TYPE_MOD_STATE_CHANGED,
    EVENT_TYPE_MOD_READY,
    EVENT_TYPE_MOD_RUNNING,
    EVENT_TYPE_MOD_ERROR,
    EVENT_TYPE_MOD_STOPPED,

    // 业务层事件 - 采集 (0x2000 - 0x2FFF)
    EVENT_TYPE_CAP_BASE = 0x2000,
    EVENT_TYPE_CAP_FRAME_READY,
    EVENT_TYPE_CAP_START,
    EVENT_TYPE_CAP_STOP,

    // 业务层事件 - AI (0x3000 - 0x3FFF)
    EVENT_TYPE_AI_BASE = 0x3000,
    EVENT_TYPE_AI_RESULT_READY,
    EVENT_TYPE_AI_START,
    EVENT_TYPE_AI_STOP,

    // 业务层事件 - 显示 (0x4000 - 0x4FFF)
    EVENT_TYPE_DISP_BASE = 0x4000,
    EVENT_TYPE_DISP_VSYNC,
    EVENT_TYPE_DISP_ERROR,

    // 自定义事件扩展 (0xF000 - 0xFFFF)
    EVENT_TYPE_CUSTOM_BASE = 0xF000,

    EVENT_TYPE_MAX = 0xFFFF
} event_type_t;

// ==========================================================================
// 4. 通用事件结构体
// ==========================================================================
typedef struct {
    event_type_t type;          // 事件类型
    event_priority_t priority;   // 事件优先级
    uint64_t timestamp;          // 时间戳（微秒）
    const char *source;          // 事件源（模块名）
    void *data;                  // 事件数据（小数据，大数据走Data Bus）
    uint32_t data_len;           // 事件数据长度
} event_t;

// ==========================================================================
// 5. 事件回调函数类型
// ==========================================================================
typedef void (*event_callback_t)(const event_t *event, void *user_data);

// ==========================================================================
// 6. 订阅者信息结构体（用于注册）
// ==========================================================================
typedef struct {
    event_type_t event_type;     // 订阅的事件类型（或 EVENT_TYPE_INVALID 表示订阅所有）
    event_callback_t callback;    // 回调函数
    void *user_data;              // 用户数据
} event_subscriber_t;

// ==========================================================================
// 7. Event Bus 配置
// ==========================================================================
typedef struct {
    uint32_t max_subscribers;     // 最大订阅者数量
} event_bus_config_t;

// ==========================================================================
// 8. 【核心】接口
// ==========================================================================

/**
 * @brief 初始化事件总线
 * @param config 配置
 * @param out_handle 输出句柄
 * @return 0成功
 */
int event_bus_init(const event_bus_config_t *config,
                   event_bus_handle_t *out_handle);

/**
 * @brief 订阅事件
 * @param handle 句柄
 * @param subscriber 订阅者信息
 * @return 订阅ID（用于取消订阅），<0失败
 */
int event_bus_subscribe(event_bus_handle_t handle,
                        const event_subscriber_t *subscriber);

/**
 * @brief 取消订阅
 * @param handle 句柄
 * @param subscription_id 订阅ID
 * @return 0成功
 */
int event_bus_unsubscribe(event_bus_handle_t handle,
                          int subscription_id);

/**
 * @brief 发布事件（核心入口）
 * @param handle 句柄
 * @param event 事件（会内部拷贝一份，调用者可立即释放）
 * @return 0成功
 */
int event_bus_publish(event_bus_handle_t handle, const event_t *event);

/**
 * @brief 【辅助】快速发布简单事件（不需要填充完整结构体）
 * @param handle 句柄
 * @param type 事件类型
 * @param source 事件源
 * @return 0成功
 */
int event_bus_publish_simple(event_bus_handle_t handle,
                             event_type_t type,
                             const char *source);

/**
 * @brief 销毁事件总线
 * @param handle 句柄
 * @return 0成功
 */
int event_bus_deinit(event_bus_handle_t handle);

/**
 * @brief 【辅助】事件类型转字符串
 * @param type 事件类型
 * @return 字符串
 */
const char* event_type_to_str(event_type_t type);

#endif /* EVENT_BUS_H */