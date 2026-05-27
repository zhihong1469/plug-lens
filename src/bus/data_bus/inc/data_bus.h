/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           data_bus.h
 * @brief          嵌入式Linux 高性能零拷贝数据总线【V4.0 标准实现】
 * @defgroup       DATA_BUS
 * @details
 *  【核心功能】
 *  1. 纯零拷贝设计：仅传递数据指针，不拷贝视频帧/AI结果等大块数据
 *  2. 双工作模式：推模式(Push)主动通知 + 拉模式(Pull)主动获取
 *  3. 多实例管理：支持最多4条独立总线(视频/音频/AI)
 *  4. 内存池化：预分配TLSF静态内存，杜绝动态分配碎片与泄漏
 *  5. C11原子引用计数：无锁高性能，绝对安全防下溢
 *
 *  【V4.0 关键改进】
 *  1. API语义明确：区分push/pull，消除歧义
 *  2. 引用计数安全：增加下溢保护，杜绝非法操作
 *  3. 函数单一职责：拆分大函数，逻辑清晰易维护
 *  4. 锁粒度优化：进一步减小临界区，并发性能提升
 *  5. 错误处理完善：全参数检查，统一错误码
 *
 *  【核心设计规则】
 *  1. 推模式：消费者必须调用data_bus_push_acquire/data_bus_release
 *  2. 拉模式：必须配对调用data_bus_pull_latest/data_bus_release
 *  3. 内存完全托管：禁止外部手动malloc/free总线内存
 *  4. 线程安全：所有对外API均支持多线程并发调用
 * @example
 *  // 推/拉模式(二者一样)生产者示例:
 * 1. 申请帧                        data_bus_alloc
 * 2. 填充数据（摄像头/数据源）      void *w_buf = data_bus_get_writable_ptr(item);
 * 3. 推送总线：【自动支持双模式】      data_bus_push
 * 4. 生产者释放自身引用          data_bus_release  
 *   // 消费者示例（推模式回调）:
 * 1. 订阅总线                        data_bus_subscribe
 * 2. 回调函数内处理数据              cb(item){1. 推模式必须：引用+1 if (data_bus_push_acquire(item) != DATA_BUS_OK) 2. 入队（异步处理，回调禁止耗时操作）}
 * 3. 工作线程: 从队列取帧->处理完成释放引用                data_bus_release
 *   // 消费者示例（拉模式）:
 * 1. 定时器/工作线程主动拉取最新数据   data_bus_pull_latest
 * 2. 处理数据后释放引用                data_bus_release
 * @author         luo
 * @date           2026
 ******************************************************************************
 */

#ifndef DATA_BUS_H
#define DATA_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vision_ai_config.h"

// ==========================================================================
// 不透明句柄（外部不可见内部结构，保证封装性）
// ==========================================================================
typedef struct data_bus_item_t* data_bus_item_handle_t;
typedef struct data_bus_subscription_t* data_bus_subscription_handle_t;

// ==========================================================================
// 推模式回调函数
// ==========================================================================
typedef void (*data_bus_callback_t)(data_bus_item_handle_t item, void *user_data);

// ==========================================================================
// 总线配置结构体
// ==========================================================================
typedef struct {
    size_t max_item_size;       // 单条数据最大大小（如视频帧：640*360*2）
    uint32_t max_items;         // 最大缓存数据项数（内存池大小，建议≥8）
    uint32_t max_subscribers;   // 最大订阅者数量（如LCD+AI+推流）
    const char *name;          // 总线唯一名称（如："video_bus"）
} data_bus_config_t;

// ==========================================================================
// 错误码定义
// ==========================================================================
#define DATA_BUS_OK              0
#define DATA_BUS_ERR_PARAM      -1  // 参数错误
#define DATA_BUS_ERR_EXIST      -2  // 总线已存在
#define DATA_BUS_ERR_FULL       -3  // 实例表/内存池/订阅表满
#define DATA_BUS_ERR_MEM        -4  // 内存分配失败
#define DATA_BUS_ERR_MAGIC      -5  // 魔法数校验失败（非法句柄）
#define DATA_BUS_ERR_STATE      -6  // 状态错误（重复发布/未发布）
#define DATA_BUS_ERR_NO_DATA    -7  // 无数据可拉取
#define DATA_BUS_ERR_TYPE       -8  // 数据类型不匹配
#define DATA_BUS_ERR_REF_UNDERFLOW -9  // 引用计数下溢

// ==========================================================================
// 对外核心API
// ==========================================================================
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief  初始化数据总线
 * @param  config: 总线配置参数
 * @return DATA_BUS_OK 成功，负数失败
 * @note   1. 必须先初始化mem_adapter(TLSF内存池)，再调用此接口
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
 * @return DATA_BUS_OK 成功，负数失败
 * @note   申请成功后必须调用data_bus_push发布，或异常时调用data_bus_release释放
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
 * @note   仅允许在data_bus_alloc后、data_bus_push前调用
 */
void* data_bus_get_writable_ptr(data_bus_item_handle_t item);
/**
 * @brief  设置数据项实际有效数据大小（编码/填充后调用）
 * @param  item: 数据项句柄
 * @param  actual_size: 实际有效数据长度
 * @return DATA_BUS_OK 成功
 */
int data_bus_set_item_size(data_bus_item_handle_t item, size_t actual_size);

/**
 * @brief  获取数据项的实际有效数据大小（适配JPEG动态长度）
 * @param  item: 数据总线句柄
 * @return 成功返回实际大小，失败返回0
 */
size_t data_bus_get_item_size(data_bus_item_handle_t item);

/**
 * @brief  发布数据（推模式+拉模式通用）
 * @param  name: 总线名称
 * @param  item: 数据项句柄
 * @return DATA_BUS_OK 成功，负数失败
 * @note   1. 发布后数据变为只读，禁止再次修改
 *         2. 推模式：自动通知所有订阅者回调
 *         3. 拉模式：更新最新数据项，供消费者拉取
 *         4. 发布后生产者必须调用data_bus_release释放初始引用
 */
int data_bus_push(const char *name, data_bus_item_handle_t item);

/* 推模式专用 引用+1 */
int data_bus_push_acquire(data_bus_item_handle_t item);
/**
 * @brief  订阅数据（推模式）
 * @param  name: 总线名称
 * @param  type: 订阅数据类型（DATA_TYPE_INVALID 接收所有类型）
 * @param  cb: 数据到达回调函数
 * @param  user_data: 用户自定义参数
 * @param  out_sub: 输出订阅句柄
 * @return DATA_BUS_OK 成功，负数失败
 * @note   回调函数内必须调用data_bus_release
 */
int data_bus_subscribe(const char *name, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_handle_t *out_sub);

/**
 * @brief  取消订阅数据
 * @param  name: 总线名称
 * @param  sub: 订阅句柄
 * @return DATA_BUS_OK 成功，负数失败
 * @note   调用后置空订阅句柄，防止野指针
 */
int data_bus_unsubscribe(const char *name, data_bus_subscription_handle_t *sub);

/**
 * @brief  拉模式：主动获取最新数据
 * @param  name: 总线名称
 * @param  type: 数据类型过滤
 * @param  out_item: 输出数据项句柄
 * @return DATA_BUS_OK 成功，负数失败
 * @note   1. 获取成功后必须配对调用data_bus_release
 *         2. 支持多线程并发拉取
 *         3. 自动舍弃所有过期帧，保证实时性
 */
int data_bus_pull_latest(const char *name,
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
 * @return DATA_BUS_OK 成功，负数失败
 *
 * @details 【核心使用场景 · 必看】
 *  1. 【推模式回调内】→ 由用户回调中手动调用
 *  2. 【拉模式使用】 → 必须调用！与data_bus_pull_latest配对使用
 *  3. 【生产者异常】 → 申请alloc后未push，可调用此函数释放
 *  4. 【生产者发布后】→ 必须调用！释放初始引用
 *
 * @note   线程安全、原子操作，无锁开销，防下溢保护
 */
int data_bus_release(data_bus_item_handle_t item);

// -------------------------------------------------------------------------
// 总线管理API
// -------------------------------------------------------------------------
/**
 * @brief  销毁总线，释放所有内存资源
 * @param  name: 总线名称
 * @return DATA_BUS_OK 成功，负数失败
 */
int data_bus_deinit(const char *name);

/**
 * @brief  数据类型转字符串（调试日志专用）
 * @param  type: 数据类型枚举
 * @return 类型字符串
 */
const char* data_type_to_str(data_type_t type);

#ifdef __cplusplus
}
#endif
#endif /* DATA_BUS_H */