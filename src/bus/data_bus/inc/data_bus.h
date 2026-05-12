#ifndef DATA_BUS_H
#define DATA_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// ==========================================================================
// 【Data Bus 核心规则】
// 1. 零拷贝：只传数据指针，不复制视频/AI大数据
// 2. 线程安全：多线程同时读写不会崩溃
// 3. 类型安全：区分RGB视频、AI结果等不同数据
// 4. 推模式(主动通知) + 拉模式(主动获取)
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄（最关键：外部看不到内部结构，只用void*指针操作）
// ==========================================================================
// 总线总句柄 → 代表整个数据总线
typedef struct data_bus_t* data_bus_handle_t;
// 数据项句柄 → 代表一条数据（RGB帧/AI结果）
typedef struct data_bus_item_t* data_bus_item_handle_t;
// 订阅句柄 → 代表一个订阅者（LCD/AI模块）
typedef struct data_bus_subscription_t* data_bus_subscription_handle_t;

// ==========================================================================
// 2. 数据类型枚举 → 用来区分不同数据（你的核心：RGB视频 + AI结果）
// ==========================================================================
typedef enum {
    DATA_TYPE_INVALID = 0,         // 无效类型

    // 视频数据
    DATA_TYPE_VIDEO_FRAME = 0x01,  // 通用视频帧
    DATA_TYPE_VIDEO_FRAME_YUV420,  // YUV格式帧
    DATA_TYPE_VIDEO_FRAME_RGB,     // 【你的核心】RGB原始帧

    // AI数据
    DATA_TYPE_AI_RESULT = 0x10,    // 【你的核心】人脸检测结果

    // 音频数据（你用不到）
    DATA_TYPE_AUDIO_FRAME = 0x20,
    DATA_TYPE_AUDIO_PCM,

    DATA_TYPE_MAX = 0xFF
} data_type_t;

// ==========================================================================
// 3. 数据元信息 → 每条数据的"身份证"
// ==========================================================================
typedef struct {
    data_type_t type;          // 数据类型（RGB/AI结果）
    uint64_t timestamp;        // 时间戳（微秒，用于同步）
    uint32_t data_size;         // 数据大小（RGB帧大小/AI结果大小）
    uint32_t ref_count;         // 引用计数（几个人在用这个数据）
    const char *producer;       // 生产者名称（采集/AI模块）
} data_bus_item_info_t;

// ==========================================================================
// 4. 推模式回调函数 → 数据来了，总线主动通知消费者
// item：数据句柄  user_data：用户自定义参数
// ==========================================================================
typedef void (*data_bus_callback_t)(data_bus_item_handle_t item, void *user_data);

// ==========================================================================
// 5. 总线配置参数 → 初始化总线时用
// ==========================================================================
typedef struct {
    uint32_t max_items;         // 最大缓存多少条数据（内存池大小）
    size_t max_item_size;       // 单条数据最大大小（RGB帧最大尺寸）
    uint32_t max_subscribers;   // 最大支持多少个订阅者（LCD+AI+推流）
} data_bus_config_t;

// ==========================================================================
// 6. 核心对外接口
// ==========================================================================

/**
 * @brief 初始化数据总线（创建内存池、锁、数组）
 * @param config 总线配置
 * @param out_handle 输出总线句柄（外部用这个操作总线）
 * @return 0成功
 */
int data_bus_init(const data_bus_config_t *config,
                  data_bus_handle_t *out_handle);

// -------------------------------------------------------------------------
// 生产者接口（采集模块/压缩模块/AI模块 用）
// -------------------------------------------------------------------------

/**
 * @brief 生产者申请一块数据内存
 * @param handle 总线句柄
 * @param type 数据类型（RGB/AI）
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
 * @brief 获取可写指针（生产者往这里写RGB/AI数据）
 * @return 可写内存地址
 */
void* data_bus_get_writable_ptr(data_bus_item_handle_t item);

/**
 * @brief 发布数据（写完数据后，通知所有消费者）
 * @return 0成功
 */
int data_bus_publish(data_bus_handle_t handle, data_bus_item_handle_t item);

// -------------------------------------------------------------------------
// 消费者接口（LCD/推流/AI模块 用）
// -------------------------------------------------------------------------

/**
 * @brief 订阅数据（推模式：数据来了自动回调）
 * @param type 订阅的数据类型（只收RGB/只收AI）
 * @param cb 回调函数
 * @param out_sub 输出订阅句柄
 * @return 0成功
 */
int data_bus_subscribe(data_bus_handle_t handle, data_type_t type,
                       data_bus_callback_t cb, void *user_data,
                       data_bus_subscription_handle_t *out_sub);

// 取消订阅
int data_bus_unsubscribe(data_bus_handle_t handle, data_bus_subscription_handle_t *sub);

/**
 * @brief 拉模式：主动获取最新数据
 * @return 0成功
 */
int data_bus_acquire_latest(data_bus_handle_t handle,
                             data_type_t type,
                             data_bus_item_handle_t *out_item);

/**
 * @brief 获取只读指针（消费者读取数据用，不能修改）
 */
const void* data_bus_get_readonly_ptr(data_bus_item_handle_t item);

// 获取数据身份证信息
int data_bus_get_item_info(data_bus_item_handle_t item,
                           data_bus_item_info_t *out_info);

/**
 * @brief 释放数据（引用计数-1，没人用就自动回收内存）
 */
int data_bus_release(data_bus_item_handle_t item);

// -------------------------------------------------------------------------
// 总线管理
// -------------------------------------------------------------------------
// 销毁总线，释放所有内存
int data_bus_deinit(data_bus_handle_t handle);

// 数据类型转字符串（打印日志用）
const char* data_type_to_str(data_type_t type);

#endif /* DATA_BUS_H */