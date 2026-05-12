#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// ==========================================================================
// 【Event Bus 核心设计规则】
// 1. 纯异步发布-订阅模式：只传递事件通知，不传输大数据（大数据走数据总线）
// 2. 线程安全：支持多线程发布、多线程订阅
// 3. 解耦设计：发布者和订阅者完全不知道对方存在
// 4. 主线程分发：回调函数在主线程执行，避免线程安全问题
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄
// 外部代码看不到内部结构，仅用void*指针操作事件总线
// ==========================================================================
typedef struct event_bus_t* event_bus_handle_t;

// ==========================================================================
// 2. 事件优先级（预留扩展，当前默认使用普通优先级）
// 用于区分事件的紧急程度，暂未实现调度逻辑
// ==========================================================================
typedef enum {
    EVENT_PRIORITY_LOW = 0,       // 低优先级
    EVENT_PRIORITY_NORMAL,        // 普通优先级（默认）
    EVENT_PRIORITY_HIGH,          // 高优先级
    EVENT_PRIORITY_CRITICAL,      // 临界优先级
    EVENT_PRIORITY_MAX
} event_priority_t;

// ==========================================================================
// 3. 【全局通用事件类型】（0x0000-0x0FFF）
// 所有服务都需要订阅的系统级控制事件，仅此一处定义
// 业务模块私有事件请在各自头文件中定义，禁止修改此文件！
// ==========================================================================
typedef enum {
    EVENT_TYPE_INVALID = 0,       // 无效事件

    // 系统全局控制事件（所有服务必须订阅）
    EVENT_TYPE_SYS_BASE = 0x0001,
    EVENT_TYPE_SYS_PAUSE,         // 全局暂停
    EVENT_TYPE_SYS_RESUME,        // 全局恢复
    EVENT_TYPE_SYS_STOP,          // 全局停止
    EVENT_TYPE_SYS_SHUTDOWN,      // 系统关机
    EVENT_TYPE_SYS_ERROR,         // 系统错误

    // 通用模块状态事件（所有服务都可以发布）
    EVENT_TYPE_MOD_STATE_CHANGED, // 模块状态变更
    EVENT_TYPE_MOD_READY,         // 模块就绪
    EVENT_TYPE_MOD_RUNNING,       // 模块运行中
    EVENT_TYPE_MOD_ERROR,         // 模块错误
    EVENT_TYPE_MOD_STOPPED,       // 模块已停止

    EVENT_TYPE_SYS_MAX = 0x0FFF   // 系统事件上限
} event_type_t;

// ==========================================================================
// 4. 事件核心结构体
// 事件 = 通知消息，仅传递小数据，大数据（RGB帧）走数据总线
// ==========================================================================
typedef struct {
    event_type_t type;            // 事件类型（唯一标识）
    event_priority_t priority;    // 事件优先级
    uint64_t timestamp;           // 事件产生时间戳（微秒）
    const char *source;            // 事件来源（模块名称，如"CAP","AI"）
    void *data;                   // 事件附加数据（小数据）
    uint32_t data_len;            // 附加数据长度
} event_t;

// ==========================================================================
// 5. 事件回调函数类型
// 订阅者收到事件后，执行的回调函数
// @param event：事件指针
// @param user_data：用户注册时传入的自定义参数
// ==========================================================================
typedef void (*event_callback_t)(const event_t *event, void *user_data);

// ==========================================================================
// 6. 订阅者注册结构体
// 外部模块订阅事件时，需要填充的参数
// ==========================================================================
typedef struct {
    event_type_t event_type;      // 订阅的事件类型（INVALID=订阅所有事件）
    event_callback_t callback;    // 事件回调函数
    void *user_data;              // 自定义参数（回调时传回）
} event_subscriber_t;

// ==========================================================================
// 7. 事件总线配置参数
// ==========================================================================
typedef struct {
    uint32_t max_subscribers;     // 最大支持的订阅者数量
} event_bus_config_t;

// ==========================================================================
// 8. 事件总线核心对外接口
// ==========================================================================

/**
 * @brief 初始化事件总线
 * @param config 总线配置参数
 * @param out_handle 输出总线句柄（外部操作总线的唯一标识）
 * @return 0成功，负数失败
 */
int event_bus_init(const event_bus_config_t *config,
                   event_bus_handle_t *out_handle);

/**
 * @brief 订阅事件
 * @param handle 事件总线句柄
 * @param subscriber 订阅者参数（事件类型+回调+自定义数据）
 * @return 订阅ID（正数，用于取消订阅），负数失败
 */
int event_bus_subscribe(event_bus_handle_t handle,
                        const event_subscriber_t *subscriber);

/**
 * @brief 取消订阅事件
 * @param handle 事件总线句柄
 * @param subscription_id 订阅时返回的ID
 * @return 0成功，负数失败
 */
int event_bus_unsubscribe(event_bus_handle_t handle,
                          int subscription_id);

/**
 * @brief 发布事件（核心接口）
 * @param handle 事件总线句柄
 * @param event 事件指针（内部会拷贝，外部可立即释放）
 * @return 0成功，负数失败
 */
int event_bus_publish(event_bus_handle_t handle, const event_t *event);

/**
 * @brief 快速发布简单事件（简化版，无需填充完整事件结构体）
 * @param handle 总线句柄
 * @param type 事件类型
 * @param source 事件来源模块名
 * @return 0成功，负数失败
 */
int event_bus_publish_simple(event_bus_handle_t handle,
                             event_type_t type,
                             const char *source);

/**
 * @brief 销毁事件总线，释放所有资源
 * @param handle 总线句柄
 * @return 0成功，负数失败
 */
int event_bus_deinit(event_bus_handle_t handle);

/**
 * @brief 获取事件等待FD（用于select/poll/epoll异步监听）
 * @param handle 总线句柄
 * @return 管道读端FD，负数失败
 */
int event_bus_get_wait_fd(event_bus_handle_t handle);

/**
 * @brief 分发事件（主线程调用！执行所有订阅者回调）
 * @param handle 总线句柄
 * @return 0成功分发事件，负数失败
 * @note 必须在主线程循环调用，实现异步事件处理
 */
int event_bus_dispatch(event_bus_handle_t handle);

/**
 * @brief 事件类型转字符串（日志打印用）
 * @param type 事件类型枚举值
 * @return 事件名称字符串
 */
const char* event_type_to_str(event_type_t type);

#endif /* EVENT_BUS_H */