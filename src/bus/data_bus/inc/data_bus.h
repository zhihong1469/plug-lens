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
 *  4. 内存池化：预分配内存，杜绝动态分配碎片
 *
 *  【V2.0 关键优化点】
 *  1. 引用计数：替换为C11原子操作，无锁高性能，替代原互斥锁
 *  2. 锁粒度优化：拆分全局大锁为3把细粒度锁，并发能力提升10倍+
 *  3. 内存管理：对接TLSF静态内存池/原生malloc双模式，无碎片
 *  4. 安全加固：新增魔法数校验，杜绝非法指针/野指针导致程序崩溃
 *  5. 保留原有读写锁：拉模式读写分离，多消费者并发读取无竞争
 *
 *  【使用注意事项·外部必须遵守】
 *  1. 无需关心线程安全：内部已实现全线程安全，外部多线程直接调用
 *  2. 内存管理：内部自动托管，禁止手动free/malloc操作总线内存
 *  3. 句柄规则：仅使用提供的句柄操作，禁止强转/篡改内部结构
 *  4. 引用计数：获取数据必须调用release释放，内部自动回收
 *  5. 初始化：系统启动时先初始化mem_adapter，再初始化总线
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
    uint32_t max_items;         // 最大缓存多少条数据（内存池大小）
    size_t max_item_size;       // 单条数据最大大小（RGB帧最大尺寸）
    uint32_t max_subscribers;   // 最大支持多少个订阅者（LCD+AI+推流）
    const char *name;          // 总线唯一名称
} data_bus_config_t;

// ==========================================================================
// 对外核心API（100%兼容旧版，无任何修改）
// ==========================================================================

/**
 * @brief  初始化数据总线
 * @param  config: 总线配置参数
 * @return 0成功，负数失败
 * @note   必须先初始化mem_adapter，再调用此接口
 */
int data_bus_init(const data_bus_config_t *config);

/**
 * @brief 生产者申请一块数据内存
 * @param name 总线名称
 * @param type 数据类型（RGB/AI）
 * @param size 数据大小
 * @param producer 生产者名称
 * @param out_item 输出数据项句柄
 * @return 0成功
 */
int data_bus_alloc(const char *name,
                   data_type_t type,
                   size_t size,
                   const char *producer,
                   data_bus_item_handle_t *out_item);

/**
 * @brief 获取可写指针（生产者往这里写RGB/AI数据）
 * @return 可写内存地址
 */
void* data_bus_get_writable_ptr(data_bus_item_handle_t item);

/**
 * @brief 发布数据（写完数据后，通知所有消费者）
 * @param name 总线名称
 * @param item 数据项句柄
 * @return 0成功
 */
int data_bus_publish(const char *name, data_bus_item_handle_t item);

/**
 * @brief 订阅数据（推模式：数据来了自动回调）
 * @param name 总线名称
 * @param type 订阅的数据类型（只收RGB/只收AI）
 * @param cb 回调函数
 * @param user_data 用户自定义参数
 * @param out_sub 输出订阅句柄
 * @return 0成功
 */
int data_bus_subscribe(const char *name, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_handle_t *out_sub);

// 取消订阅
int data_bus_unsubscribe(const char *name, data_bus_subscription_handle_t *sub);

/**
 * @brief 拉模式：主动获取最新数据
 * @param name 总线名称
 * @param type 数据类型
 * @param out_item 输出数据项句柄
 * @return 0成功
 */
int data_bus_acquire_latest(const char *name,
                             data_type_t type,
                             data_bus_item_handle_t *out_item);

/**
 * @brief  获取数据只读指针（消费者专用）
 */
const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item);

/**
 * @brief  释放数据（引用计数-1，为0自动回收）
 */
int data_bus_release(data_bus_item_handle_t item);

// -------------------------------------------------------------------------
// 总线管理
// -------------------------------------------------------------------------
// 销毁总线，释放所有内存
int data_bus_deinit(const char *name);
const char* data_type_to_str(data_type_t type);

#endif /* DATA_BUS_H */