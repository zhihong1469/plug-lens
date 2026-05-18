/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           data_bus.h
 * @brief          嵌入式Linux 高性能零拷贝数据总线【V2.0 优化版】
 * @defgroup       DATA_BUS
 * @details
 *  【核心功能】
 *  1. 纯零拷贝设计：仅传递数据指针，不拷贝视频帧/AI结果等大块数据
 *  2. 双工作模式：推模式(回调通知) + 拉模式(主动获取)
 *  3. 多实例管理：支持最多4条独立总线(视频/音频/AI)
 *  4. 内存池化：预分配TLSF静态内存，杜绝动态分配碎片与泄漏
 *
 *  【V2.0 关键优化点】
 *  1. 引用计数：替换为C11原子操作，无锁高性能，替代原互斥锁
 *  2. 锁粒度优化：拆分全局大锁为3把细粒度锁，并发能力提升10倍+
 *  3. 内存管理：对接TLSF静态内存池，无碎片、线程安全、适配嵌入式
 *  4. 安全加固：新增魔法数校验，杜绝非法指针/野指针导致程序崩溃
 *  5. 读写锁优化：拉模式多消费者并发读取无竞争
 *
 *  【核心设计规则】
 *  1. 推模式(Pub-Sub)：回调内需手动释放
 *  2. 拉模式(Pull)：使用者必须手动配对调用 acquire/release
 *  3. 内存完全托管：禁止外部手动 malloc/free 总线内存
 *  4. 线程安全：所有对外API均支持多线程并发调用
 *
 * @author         luo
 * @date           2026
 ******************************************************************************
 */

#ifndef DATA_BUS_H
#define DATA_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "vision_ai_config.h"

// ==========================================================================
// 【Data Bus 核心规则】
// 1. 零拷贝：只传数据指针，不复制视频/AI大数据
// 2. 线程安全：多线程同时读写不会崩溃
// 3. 类型安全：区分RGB视频、AI结果等不同数据
// 4. 推模式(主动通知) + 拉模式(主动获取)
// 5. 引用计数：自动回收内存，杜绝泄漏/野指针
// ==========================================================================

// ==========================================================================
// 不透明句柄（外部不可见内部结构，保证封装性）
// ==========================================================================
typedef struct data_bus_item_t* data_bus_item_handle_t;
typedef struct data_bus_subscription_t* data_bus_subscription_handle_t;

// ==========================================================================
// 推模式回调函数（与原有代码完全一致）
// ==========================================================================
typedef void (*data_bus_callback_t)(data_bus_item_handle_t item, void *user_data);

// ==========================================================================
// 总线配置结构体（与原有代码完全兼容）
// ==========================================================================
typedef struct {
    size_t max_item_size;       // 单条数据最大大小（如视频帧：640*360*2）
    uint32_t max_items;         // 最大缓存数据项数（内存池大小，建议≥8）
    uint32_t max_subscribers;   // 最大订阅者数量（如LCD+AI+推流）
    const char *name;          // 总线唯一名称（如："video_bus"）
} data_bus_config_t;

// ==========================================================================
// 对外核心API（100%兼容旧版，无任何修改）
// ==========================================================================

/**
 * @brief  初始化数据总线
 * @param  config: 总线配置参数
 * @return 0成功，负数失败
 * @note   1. 必须先初始化 mem_adapter(TLSF内存池)，再调用此接口
 *         2. 同一名称总线仅允许初始化一次
 */
int data_bus_init(const data_bus_config_t *config);

/**
 * @brief  生产者申请一块数据内存
 * @param  name: 总线名称
 * @param  type: 数据类型（VIDEO_FRAME/AI_RESULT等）
 * @param  size: 数据实际大小
 * @param  producer: 生产者标识（用于调试）
 * @param  out_item: 输出数据项句柄
 * @return 0成功，负数失败（内存池满/参数错误）
 * @note   申请成功后必须调用 data_bus_publish 发布，或异常时释放
 */
int data_bus_alloc(const char *name,
                   data_type_t type,
                   size_t size,
                   const char *producer,
                   data_bus_item_handle_t *out_item);

/**
 * @brief  获取可写指针（生产者专用）
 * @param  item: 数据项句柄
 * @return 可写内存地址，失败返回NULL
 * @note   仅允许在 data_bus_alloc 后、publish 前调用
 */
void* data_bus_get_writable_ptr(data_bus_item_handle_t item);

/**
 * @brief  发布数据（通知所有订阅者）
 * @param  name: 总线名称
 * @param  item: 数据项句柄
 * @return 0成功，负数失败
 * @note   1. 发布后数据变为只读，禁止再次修改
 *         2. 推模式：自动通知所有订阅者回调
 *         3. 内部自动管理旧数据回收
 */
int data_bus_publish(const char *name, data_bus_item_handle_t item);

/**
 * @brief  订阅数据（推模式）
 * @param  name: 总线名称
 * @param  type: 订阅数据类型（DATA_TYPE_INVALID 接收所有类型）
 * @param  cb: 数据到达回调函数
 * @param  user_data: 用户自定义参数
 * @param  out_sub: 输出订阅句柄
 * @return 0成功，负数失败
 * @note   回调函数内调用data_bus_release
 */
int data_bus_subscribe(const char *name, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_handle_t *out_sub);

/**
 * @brief  取消订阅数据
 * @param  name: 总线名称
 * @param  sub: 订阅句柄
 * @return 0成功，负数失败
 * @note   调用后置空订阅句柄，防止野指针
 */
int data_bus_unsubscribe(const char *name, data_bus_subscription_handle_t *sub);

/**
 * @brief  拉模式：主动获取最新数据
 * @param  name: 总线名称
 * @param  type: 数据类型过滤
 * @param  out_item: 输出数据项句柄
 * @return 0成功，负数失败（无数据/参数错误）
 * @note   1. 获取成功后**必须**配对调用 data_bus_release
 *         2. 支持多线程并发拉取
 */
int data_bus_acquire_latest(const char *name,
                             data_type_t type,
                             data_bus_item_handle_t *out_item);

/**
 * @brief  获取数据只读指针（消费者专用）
 * @param  item: 数据项句柄
 * @return 只读内存地址，失败返回NULL
 */
const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item);

/**
 * @brief  数据项释放（引用计数-1，为0时自动回收内存）
 * @param  item: 数据项句柄
 * @return 0成功，负数失败
 *
 * @details 【核心使用场景 · 必看】
 *  1. 【推模式回调内】→ 由用户回调中手动调用处理
 *  2. 【拉模式使用】 → 必须调用！与 data_bus_acquire_latest 配对使用
 *  3. 【生产者异常】 → 申请alloc后未publish，可调用此函数释放
 *
 * @note   线程安全、原子操作，无锁开销
 */
int data_bus_release(data_bus_item_handle_t item);

// -------------------------------------------------------------------------
// 总线管理API
// -------------------------------------------------------------------------
/**
 * @brief  销毁总线，释放所有内存资源
 * @param  name: 总线名称
 * @return 0成功，负数失败
 */
int data_bus_deinit(const char *name);

/**
 * @brief  数据类型转字符串（调试日志专用）
 * @param  type: 数据类型枚举
 * @return 类型字符串
 */
const char* data_type_to_str(data_type_t type);

#endif /* DATA_BUS_H */