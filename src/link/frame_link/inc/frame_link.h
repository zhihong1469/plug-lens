/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           frame_link.h
 * @brief          FrameLink 多实例数据链路层 对外接口头文件
 * @defgroup       FrameLink
 * @brief          高性能零拷贝帧数据链路层（基于TLSF静态内存池）
 * @details
 *  1. 多实例命名管理：支持最多4条独立数据链路，按名称区分
 *  2. 静态内存池：全程使用TLSF静态内存适配器，无动态堆分配
 *  3. 原子引用计数：线程安全，单写多读，零拷贝传输帧数据
 *  4. 双缓存架构：内存池(帧存储) + 队列(帧调度)
 *  5. 统一释放机制：生产者/消费者共用 frame_link_put 释放帧
 *  6. 线程安全：内置超时互斥锁，支持多线程并发访问
 *  7. 适用场景：视频采集、AI推理、数据总线、消息队列等零拷贝场景
 *
 * @attention
 *  1. 必须先调用 mem_init() 初始化静态内存适配器，再使用本模块
 *  2. 所有内存操作基于 mem_adapter.h 静态接口，禁止混用原生 malloc/free
 *  3. 帧获取后必须调用 frame_link_put 释放，禁止手动释放内存
 *  4. 单生产者多消费者模型，生产者唯一，消费者可多个
 *
 * @author         FrameLink System Team
 * @date           2025
 ******************************************************************************
 */
#ifndef __FRAME_LINK_H
#define __FRAME_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== 模块配置宏定义 ========================== */
/** @defgroup FL_CONFIG FrameLink 编译配置
 *  @{
 */
#define FRAME_LINK_NAME_MAX_LEN        32U     /**< 数据链路名称最大长度（含结束符） */
#define FRAME_LINK_MAX_INSTANCES       4U      /**< 系统最大支持的独立链路实例数量 */
#define FRAME_LINK_POOL_MIN            1U      /**< 单链路内存池最小帧数 */
#define FRAME_LINK_POOL_MAX            8U      /**< 单链路内存池最大帧数 */
#define FRAME_LINK_LOCK_TIMEOUT_MS     10U     /**< 互斥锁超时时间（单位：ms），避免死锁 */
/** @} */

/* ========================== 模块错误码枚举 ========================== */
/** @defgroup FL_ERR FrameLink 错误返回码
 *  @brief 所有接口返回值定义，0=成功，负数=失败
 *  @{
 */
typedef enum {
    FL_OK               = 0,      /**< 执行成功 */
    FL_INVALID_PARAM    = -1,     /**< 入参无效（NULL/越界/格式错误） */
    FL_NO_MEM           = -2,     /**< 静态内存分配失败（内存池不足） */
    FL_NOT_FOUND        = -3,     /**< 未找到指定名称的链路实例 */
    FL_BUSY             = -4,     /**< 链路已存在/实例已满 */
    FL_TIMEOUT          = -5,     /**< 互斥锁加锁超时（线程死锁风险） */
    FL_NO_FREE_FRAME    = -6,     /**< 内存池无空闲帧（生产者获取失败） */
} fl_err_t;
/** @} */

/* ========================== 帧数据格式枚举 ========================== */
/** @defgroup FL_FMT 支持的帧数据格式
 *  @brief 视频/图像帧标准格式定义
 *  @{
 */
typedef enum {
    FRAME_FMT_YUYV      = 0,      /**< YUYV 格式（摄像头默认输出） */
    FRAME_FMT_NV12      = 1,      /**< NV12 格式（AI推理常用） */
    FRAME_FMT_MJPEG     = 2,      /**< MJPEG 压缩格式 */
    FRAME_FMT_RGB888    = 3,      /**< RGB888 原始格式（显示屏专用） */
} frame_format_t;
/** @} */

/* ========================== 消费者获取模式枚举 ========================== */
/** @defgroup FL_CONSUME 消费者帧获取模式
 *  @brief 消费者读取帧的两种策略
 *  @{
 */
typedef enum {
    FL_CONSUME_LATEST   = 0,      /**< 【推荐】直接获取最新帧，丢弃历史数据（实时场景） */
    FL_CONSUME_QUEUE    = 1,      /**< 从FIFO队列顺序取帧（有序数据场景） */
} fl_consume_mode_t;
/** @} */

/* ========================== 核心数据结构体 ========================== */
/**
 * @brief 帧元数据结构体
 * @note  存储帧的描述信息，由内核自动管理时间戳/帧ID，禁止外部修改
 */
typedef struct {
    uint64_t        timestamp_us;   // 时间戳(us)  8B
    uint32_t        width;          // 帧宽度      4B
    uint32_t        height;         // 帧高度      4B
    uint32_t        data_size;      // 数据长度    4B
    uint32_t        frame_id;       // 帧ID        4B
    frame_format_t  format;         // 数据格式    4B
} frame_info_t;
/**
 * @brief 帧链路句柄（外部透明指针）
 * @note  上层无需关注内部实现，仅作为句柄传递
 */
typedef struct frame_link *frame_link_handle_t;

/**
 * @brief 帧数据句柄（外部透明指针）
 * @note  帧的唯一操作标识，所有帧操作基于该句柄
 */
typedef struct frame *frame_handle_t;

/**
 * @brief FrameLink 链路创建配置结构体
 * @note  创建链路时必须填充该结构体
 */
typedef struct {
    char                name[FRAME_LINK_NAME_MAX_LEN];  /**< 链路唯一名称（用于查找）*/
    uint32_t            max_frame_size;                /**< 单帧最大数据缓冲区大小（字节）*/
    uint32_t            pool_count;                    /**< 内存池帧数（1~8，@ref FRAME_LINK_POOL_MIN/MAX）*/
    uint32_t            queue_count;                   /**< 帧队列深度（缓存等待消费的帧）*/
} frame_link_cfg_t;

/* ========================== 全局核心接口 ========================== */
/**
 * @brief  创建一条命名的 FrameLink 数据链路
 * @param[in]  cfg: 链路配置结构体指针 @ref frame_link_cfg_t
 * @return 执行结果 @ref fl_err_t
 * @note
 *  - 全局初始化接口，系统启动时调用
 *  - 基于静态内存池分配所有资源，无动态内存
 *  - 链路名称唯一，重复创建返回 FL_BUSY
 */
fl_err_t frame_link_create(const frame_link_cfg_t *cfg);

/**
 * @brief  销毁指定名称的 FrameLink 数据链路
 * @param[in]  name: 链路名称
 * @return 执行结果 @ref fl_err_t
 * @note   释放所有静态内存、销毁互斥锁、清空实例
 */
fl_err_t frame_link_destroy(const char *name);

/**
 * @brief  清空指定链路的所有帧数据（重置内存池）
 * @param[in]  name: 链路名称
 * @return 执行结果 @ref fl_err_t
 * @note   引用计数清零，队列清空，帧恢复初始状态（不释放内存）
 */
fl_err_t frame_link_clear(const char *name);

/* ========================== 生产者专用接口 ========================== */
/**
 * @brief  生产者从内存池获取一个空闲帧
 * @param[in]  name: 链路名称
 * @param[out] out_frame: 输出帧句柄指针
 * @return 执行结果 @ref fl_err_t
 * @note
 *  - 获取成功后帧为**可写状态**，引用计数=1
 *  - 无空闲帧返回 FL_NO_FREE_FRAME
 *  - 使用完成后必须调用 frame_link_put 释放
 */
fl_err_t frame_link_producer_get(const char *name, frame_handle_t *out_frame);

/**
 * @brief  生产者将填充好的帧推入链路队列
 * @param[in]  name: 链路名称
 * @param[in]  frame: 帧句柄（由 producer_get 获取）
 * @return 执行结果 @ref fl_err_t
 * @note   推入后帧变为**只读状态**，消费者可读取
 */
fl_err_t frame_link_producer_push(const char *name, frame_handle_t frame);

/**
 * @brief  获取帧的可写数据指针
 * @param[in]  frame: 帧句柄
 * @return 成功返回数据指针，失败/只读返回NULL
 * @note   仅生产者可调用，必须在 push 之前使用
 */
void *frame_get_writable_ptr(frame_handle_t frame);

/**
 * @brief  设置帧元数据（仅可设置分辨率/格式/大小）
 * @param[in]  frame: 帧句柄
 * @param[in]  info: 元数据指针
 * @return 执行结果 @ref fl_err_t
 * @note
 *  - 禁止覆盖 frame_id / timestamp_us（内核自动生成）
 *  - 仅可写状态下调用
 */
fl_err_t frame_set_info(frame_handle_t frame, const frame_info_t *info);

/* ========================== 消费者专用接口 ========================== */
/**
 * @brief  从数据总线直接获取帧（零拷贝复用）
 * @param[in]  name: 链路名称
 * @param[in]  bus_frame: 总线传递的帧句柄
 * @param[out] out_frame: 输出新的帧句柄
 * @return 执行结果 @ref fl_err_t
 * @note   原子引用计数+1，线程安全
 */
fl_err_t frame_link_consumer_get_by_bus(const char *name, frame_handle_t bus_frame, frame_handle_t *out_frame);

/**
 * @brief  消费者从链路获取帧（标准接口）
 * @param[in]  name: 链路名称
 * @param[in]  mode: 获取模式 @ref fl_consume_mode_t
 * @param[out] out_frame: 输出帧句柄指针
 * @return 执行结果 @ref fl_err_t
 * @note   获取成功后引用计数+1，必须调用 put 释放
 */
fl_err_t frame_link_consumer_get(const char *name, fl_consume_mode_t mode, frame_handle_t *out_frame);

/**
 * @brief  【通用】帧引用计数释放接口（生产者/消费者统一调用）
 * @param[in]  frame: 帧句柄
 * @return 执行结果 @ref fl_err_t
 * @note
 *  - 核心接口：引用计数-1，计数=0时自动回收至内存池
 *  - 生产者/消费者必须统一使用该接口释放帧
 *  - 禁止重复释放、禁止手动释放内存
 */
fl_err_t frame_link_put(frame_handle_t frame);

/**
 * @brief  获取帧的只读数据指针
 * @param[in]  frame: 帧句柄
 * @return 成功返回只读数据指针，失败返回NULL
 * @note   消费者专用，禁止写入数据
 */
const void *frame_get_readonly_ptr(frame_handle_t frame);

/**
 * @brief  获取帧完整元数据
 * @param[in]  frame: 帧句柄
 * @param[out] info: 元数据输出指针
 * @return 执行结果 @ref fl_err_t
 */
fl_err_t frame_get_info(frame_handle_t frame, frame_info_t *info);

/* ========================== 使用示例（核心参考） ========================== */
/**
 * @page FL_USAGE FrameLink 标准使用示例
 *
 * @section FL_INIT 1. 模块初始化（系统启动）
 * @code
 * #include "mem_adapter.h"
 * #include "frame_link.h"
 *
 * // 1. 初始化静态内存池（必须第一步）
 * static uint8_t g_static_pool[1024*1024*4]; // 4M静态内存
 * mem_init(g_static_pool, sizeof(g_static_pool));
 *
 * // 2. 创建视频采集链路
 * frame_link_cfg_t cfg = {
 *     .name = "main_cam",
 *     .max_frame_size = 460800, // 640*360*2 YUYV
 *     .pool_count = 8,
 *     .queue_count = 4
 * };
 * frame_link_create(&cfg);
 * @endcode
 *
 * @section FL_PRODUCER 2. 生产者示例（摄像头采集）
 * @code
 * frame_handle_t frame = NULL;
 * // 1. 获取空闲帧
 * if (frame_link_producer_get("main_cam", &frame) == FL_OK) {
 *     // 2. 获取可写指针
 *     uint8_t *w_buf = frame_get_writable_ptr(frame);
 *     // 3. 填充帧数据（摄像头拷贝数据）
 *     memcpy(w_buf, cam_data, cam_size);
 *     // 4. 设置帧元数据
 *     frame_info_t info = {
 *         .width = 640, .height = 360,
 *         .format = FRAME_FMT_YUYV,
 *         .data_size = cam_size
 *     };
 *     frame_set_info(frame, &info);
 *     // 5. 推入队列
 *     frame_link_producer_push("main_cam", frame);
 *     // 6. 释放生产者引用
 *     frame_link_put(frame);
 * }
 * @endcode
 *
 * @section FL_CONSUMER 3. 消费者示例（AI推理）
 * @code
 * frame_handle_t frame = NULL;
 * // 1. 获取最新帧
 * if (frame_link_consumer_get("main_cam", FL_CONSUME_LATEST, &frame) == FL_OK) {
 *     // 2. 获取只读指针
 *     const uint8_t *r_buf = frame_get_readonly_ptr(frame);
 *     // 3. 读取元数据
 *     frame_info_t info;
 *     frame_get_info(frame, &info);
 *     // 4. 业务处理（AI推理）
 *     ai_infer(r_buf, info.width, info.height);
 *     // 5. 释放消费者引用
 *     frame_link_put(frame);
 * }
 * @endcode
 */

#ifdef __cplusplus
}
#endif

#endif /* __FRAME_LINK_H */