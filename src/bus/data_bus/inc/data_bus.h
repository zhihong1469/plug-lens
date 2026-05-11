#ifndef DATA_BUS_H
#define DATA_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// ==========================================================================
// 【Data Bus 铁律】
// 1. 零拷贝：只传递指针，引用计数管理生命周期
// 2. 线程安全：引用计数操作原子化
// 3. 类型安全：支持多种数据类型，带类型校验
// 4. 支持 推模式(订阅回调) + 拉模式(主动获取) 双架构
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄
// ==========================================================================
typedef void* data_bus_handle_t;
typedef void* data_bus_item_handle_t;
typedef void* data_bus_subscription_t; // 订阅句柄（补全）

// ==========================================================================
// 2. 数据类型枚举（修复重复定义）
// ==========================================================================
typedef enum {
    DATA_TYPE_INVALID = 0,

    // 视频数据 (0x01 - 0x0F)
    DATA_TYPE_VIDEO_FRAME = 0x01,
    DATA_TYPE_VIDEO_FRAME_YUV420,
    DATA_TYPE_VIDEO_FRAME_RGB,

    // AI数据 (0x10 - 0x1F)
    DATA_TYPE_AI_RESULT = 0x10,

    // 音频数据 (0x20 - 0x2F)
    DATA_TYPE_AUDIO_FRAME = 0x20,
    DATA_TYPE_AUDIO_PCM,

    // 自定义扩展 (0xF0 - 0xFF)
    DATA_TYPE_CUSTOM_BASE = 0xF0,
    DATA_TYPE_MAX = 0xFF
} data_type_t;

// ==========================================================================
// 3. 数据项元信息
// ==========================================================================
typedef struct {
    data_type_t type;          // 数据类型
    uint64_t timestamp;         // 时间戳（微秒）
    uint32_t data_size;         // 数据大小（字节）
    uint32_t ref_count;         // 当前引用计数（只读）
    const char *producer;       // 生产者名称
} data_bus_item_info_t;

// ==========================================================================
// 4. 数据总线回调类型（补全：推模式核心）
// ==========================================================================
typedef void (*data_bus_callback_t)(data_bus_item_handle_t item, void *user_data);

// ==========================================================================
// 5. Data Bus 配置
// ==========================================================================
typedef struct {
    uint32_t max_items;         // 最大数据项数量（池大小）
    size_t max_item_size;       // 单个数据项最大大小
    uint32_t max_subscribers; // 新增：最大订阅者
} data_bus_config_t;

// ==========================================================================
// 5. 【核心】接口
// ==========================================================================

/**
 * @brief 初始化数据总线
 * @param config 配置
 * @param out_handle 输出句柄
 * @return 0成功
 */
int data_bus_init(const data_bus_config_t *config,
                  data_bus_handle_t *out_handle);

// -------------------------------------------------------------------------
// 生产者接口
// -------------------------------------------------------------------------

/**
 * @brief 申请一个数据项（生产者用）
 * @param handle 句柄
 * @param type 数据类型
 * @param size 数据大小
 * @param producer 生产者名称
 * @param out_item 输出数据项句柄
 * @return 0成功
 */
int data_bus_alloc(data_bus_handle_t handle,
                   data_type_t type,
                   size_t size,
                   const char *producer,
                   data_bus_item_handle_t *out_item);

/**
 * @brief 获取数据项的可写指针（生产者用，填充数据）
 * @param item 数据项句柄
 * @return 可写指针，NULL失败
 */
void* data_bus_get_writable_ptr(data_bus_item_handle_t item);

/**
 * @brief 发布数据项（发布后，生产者不应再修改数据）
 * @param handle 句柄
 * @param item 数据项句柄
 * @return 0成功
 */
int data_bus_publish(data_bus_handle_t handle, data_bus_item_handle_t item);

// -------------------------------------------------------------------------
// 消费者接口
// -------------------------------------------------------------------------

// 消费者 - 推模式（订阅回调，人脸服务必需）
int data_bus_subscribe(data_bus_handle_t handle, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_t *out_sub);
int data_bus_unsubscribe(data_bus_handle_t handle, data_bus_subscription_t *sub);

/**
 * @brief 获取最新的数据项（消费者用，引用计数+1）
 * @param handle 句柄
 * @param type 期望的数据类型
 * @param out_item 输出数据项句柄
 * @return 0成功
 */
int data_bus_acquire_latest(data_bus_handle_t handle,
                             data_type_t type,
                             data_bus_item_handle_t *out_item);

/**
 * @brief 获取数据项的只读指针（消费者用）
 * @param item 数据项句柄
 * @return 只读指针，NULL失败
 */
const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item);

/**
 * @brief 获取数据项信息
 * @param item 数据项句柄
 * @param out_info 输出信息
 * @return 0成功
 */
int data_bus_get_item_info(data_bus_item_handle_t item,
                           data_bus_item_info_t *out_info);

/**
 * @brief 释放数据项（引用计数-1，计数为0时自动回收）
 * @param item 数据项句柄
 * @return 0成功
 */
int data_bus_release(data_bus_item_handle_t item);

// -------------------------------------------------------------------------
// 管理接口
// -------------------------------------------------------------------------

/**
 * @brief 销毁数据总线
 * @param handle 句柄
 * @return 0成功
 */
int data_bus_deinit(data_bus_handle_t handle);

/**
 * @brief 【辅助】数据类型转字符串
 * @param type 类型
 * @return 字符串
 */
const char* data_type_to_str(data_type_t type);

#endif /* DATA_BUS_H */