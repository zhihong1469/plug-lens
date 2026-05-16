/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           event_bus.h
 * @brief          嵌入式Linux 高性能异步事件总线【V2.0 优化版】
 * @defgroup       EVENT_BUS
 * @details
 *  【核心功能】
 *  1. 纯异步事件驱动：队列缓存+管道唤醒，无阻塞、无死循环
 *  2. 发布-订阅模式：支持多实例、多订阅者、事件类型过滤
 *  3. 自订阅过滤：可配置是否接收自己发布的事件，避免逻辑冲突
 *  4. 主线程分发：回调统一在主线程执行，彻底解决并发冲突/死锁
 *  5. 线程安全：多线程任意发布/订阅，无竞争风险
 *
 *  【V2.0 关键优化点】
 *  1. 内存管理：对接TLSF静态内存池/原生malloc双模式，杜绝内存碎片
 *  2. 锁粒度优化：拆分全局大锁为订阅锁/队列锁，并发能力大幅提升
 *  3. 原子操作：事件统计计数使用C11原子变量，无锁高性能更新
 *  4. 安全加固：新增魔法数校验，拦截非法指针/野指针，永不崩溃
 *  5. 内存托管：所有内存由内部自动管理，禁止外部手动分配/释放
 *
 *  【使用注意事项·外部必须遵守】
 *  1. 初始化顺序：系统启动先初始化mem_adapter，再初始化事件总线
 *  2. 线程规则：event_bus_dispatch() 必须在主线程调用，回调无并发风险
 *  3. 内存规则：事件结构体由总线自动拷贝/释放，外部无需管理
 *  4. 并发规则：内部全线程安全，外部多线程直接调用，无需加锁
 *  5. 句柄规则：订阅ID/总线名称为唯一标识，禁止篡改内部结构
 *
 * @author         luo
 * @date           2026
 ******************************************************************************
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "vision_ai_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 公共类型定义
// ==========================================================================

/**
 * @brief 事件优先级枚举
 * @note 预留扩展，当前默认使用普通优先级
 */
typedef enum {
    EVENT_PRIORITY_LOW       = 0,    /**< 低优先级事件 */
    EVENT_PRIORITY_NORMAL    = 1,    /**< 普通优先级（默认） */
    EVENT_PRIORITY_HIGH      = 2,    /**< 高优先级事件 */
    EVENT_PRIORITY_CRITICAL  = 3,    /**< 临界紧急事件 */
    EVENT_PRIORITY_MAX       = 4     /**< 优先级上限 */
} event_priority_t;

/**
 * @brief 事件核心结构体
 * @details 用于模块间传递事件通知，小数据量传输
 *          大数据请使用数据总线，事件总线仅传递通知
 */
typedef struct {
    event_type_t      type;         /**< 事件类型（全局唯一枚举） */
    event_priority_t  priority;     /**< 事件优先级 */
    const char       *source;       /**< 事件发布者标识（模块名） */
    uint64_t          timestamp;    /**< 事件产生时间戳(us) */
    void             *data;         /**< 事件附加数据指针 */
    uint32_t          data_len;     /**< 附加数据长度 */
} event_t;

/**
 * @brief 事件回调函数类型
 * @param event  事件指针（总线内部管理，无需释放）
 * @param user_data 订阅时传入的自定义数据
 */
typedef void (*event_callback_t)(const event_t *event, void *user_data);

/**
 * @brief 订阅者配置结构体
 * @details 用于向总线注册事件监听
 */
typedef struct {
    event_type_t      event_type;        /**< 订阅的事件类型，INVALID=订阅所有事件 */
    event_callback_t  callback;          /**< 事件触发回调函数 */
    void             *user_data;         /**< 回调自定义参数 */
    bool              skip_self_published;/**< 是否跳过自己发布的事件：true=跳过，false=接收 */
} event_subscriber_t;

/**
 * @brief 事件总线初始化配置
 */
typedef struct {
    uint32_t    max_subscribers;  /**< 总线最大支持订阅者数量 */
    const char *name;             /**< 总线唯一名称（多实例区分） */
} event_bus_config_t;

// ==========================================================================
// 对外核心接口（完整注释）
// ==========================================================================

/**
 * @brief 初始化事件总线实例
 * @param config 总线配置参数（名称+最大订阅数）
 * @return 0成功，负数失败
 * @note 系统启动时调用一次，支持多实例
 */
int event_bus_init(const event_bus_config_t *config);

/**
 * @brief 标准订阅接口（兼容旧代码）
 * @param name        总线名称
 * @param subscriber  订阅者配置
 * @return 订阅ID(>0)，负数失败
 * @note 默认开启：跳过自己发布的事件，订阅者ID=DEFAULT
 */
int event_bus_subscribe(const char *name, const event_subscriber_t *subscriber);

/**
 * @brief 扩展订阅接口（支持发布者自订阅控制）
 * @param name          总线名称
 * @param subscriber    订阅者配置（可配置skip_self_published）
 * @param subscriber_id 订阅者唯一标识（推荐使用模块名）
 * @return 订阅ID(>0)，负数失败
 * @note 支持自定义是否接收自己发布的事件，用于故障处理/自检场景
 */
int event_bus_subscribe_ex(const char *name, const event_subscriber_t *subscriber, const char *subscriber_id);

/**
 * @brief 取消事件订阅
 * @param name              总线名称
 * @param subscription_id   订阅时返回的ID
 * @return 0成功，负数失败
 */
int event_bus_unsubscribe(const char *name, int subscription_id);

/**
 * @brief 发布异步事件
 * @param name  总线名称
 * @param event 事件结构体指针（内部拷贝，外部可立即释放）
 * @return 0成功，负数失败
 * @note 线程安全，多线程可调用
 */
int event_bus_publish(const char *name, const event_t *event);

/**
 * @brief 快速发布简单事件（无附加数据）
 * @param name    总线名称
 * @param type    事件类型
 * @param source  事件发布者标识（模块名）
 * @return 0成功，负数失败
 */
int event_bus_publish_simple(const char *name, event_type_t type, const char *source);

/**
 * @brief 获取事件总线监听FD（用于select/poll）
 * @param name 总线名称
 * @return 管道读端FD，负数失败
 */
int event_bus_get_wait_fd(const char *name);

/**
 * @brief 分发事件（主线程必须调用）
 * @param name 总线名称
 * @return 0成功，负数失败
 * @note 仅在主线程调用，执行所有订阅者回调
 */
int event_bus_dispatch(const char *name);

/**
 * @brief 销毁事件总线，释放所有资源
 * @param name 总线名称
 * @return 0成功，负数失败
 */
int event_bus_deinit(const char *name);

/**
 * @brief 事件类型转字符串（日志打印）
 * @param type 事件类型枚举
 * @return 事件名称字符串
 */
const char* event_type_to_str(event_type_t type);

/**
 * @brief 获取事件发布者标识
 * @param event 事件指针
 * @return 发布者模块名字符串
 */
const char* event_get_source(const event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_H */