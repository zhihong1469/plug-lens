/* SPDX-License-Identifier: MIT */
/**
 * @file    frame_link.h
 * @brief   嵌入式Linux 视觉帧数据链路层（命名化多实例版）
 * @details
 *  【架构定位】系统级公共基础组件，位于双总线与业务服务之间
 *  【核心能力】静态内存池 + 原子引用计数 + 线程安全队列 + 帧生命周期托管
 *  【设计范式】对标数据总线：命名化管理、多实例隔离、无全局变量、句柄透明
 *  【核心保障】被引用帧(ref_cnt>0)永久保护，绝不回收/覆盖，彻底杜绝野指针
 *  【使用场景】单/多摄像头帧管理，采集(生产者) + AI/显示/录像/推流(消费者) 全局共享
 *  【线程安全】全接口线程安全，支持多线程并发引用/释放/获取帧
 *  【内存策略】静态分配无碎片，池满丢新保旧，不阻塞采集线程
 * 
 * 【多实例设计】
 *  1. 支持创建多个独立帧链路（如：main_cam、sub_cam）
 *  2. 所有模块通过**链路名称**访问，无全局变量、无模块耦合
 *  3. 实例相互隔离，内存池/队列独立管理
 * 
 * 【使用铁律（强制遵守）】
 *  1. 禁止直接操作 ref_cnt，必须使用 ref/unref 标准接口
 *  2. 生产者(采集)：仅可调用 get/enqueue/return 接口
 *  3. 消费者(业务)：仅可调用 dequeue/ref/unref 接口
 *  4. 谁引用谁释放，ref/unref 必须成对调用
 *  5. 禁止直接传递帧指针，禁止手动free帧内存
 *  6. 数据总线仅转发帧指针，回调仅做 ref + 入队，无耗时操作
 * 
 * 【标准工作流】
 *  生产者(采集)：frame_link_get_free_frame → 填充数据 → frame_link_enqueue_frame → 发布总线
 *  消费者(业务)：总线取帧 → frame_link_ref_frame → 业务处理 → frame_link_unref_frame
 *  自动回收：ref_cnt=0 时，帧自动归还内存池
 * 
 * @author  Luo
 * @date    2026-05-31
 * @version 3.0 (命名化多实例重构版)
 */

#ifndef __FRAME_LINK_H
#define __FRAME_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include "pool.h"
#include "queue.h"
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 系统配置宏（命名化管理参数，对标数据总线）
// ==========================================================================
#define FRAME_LINK_MAX_INSTANCES    4       /**< 最大支持帧链路实例数 */
#define FRAME_LINK_NAME_MAX_LEN      16      /**< 帧链路名称最大长度 */

// ==========================================================================
// @brief 帧像素格式枚举（全系统通用标准格式）
// @note  所有模块统一使用该枚举，禁止自定义格式
// ==========================================================================
typedef enum {
    FRAME_FMT_YUYV   = 0,    /**< YUYV 4:2:2 摄像头默认格式 */
    FRAME_FMT_NV12   = 1,    /**< NV12 YUV420 硬编解码常用 */
    FRAME_FMT_RGB888 = 2,    /**< RGB24 通用显示格式 */
    FRAME_FMT_BGR888 = 3     /**< BGR24 OpenCV/AI模型默认输入格式 */
} frame_format_t;

// ==========================================================================
// @brief 全系统标准帧结构体（不可修改成员）
// @note  ref_cnt 为私有成员，禁止直接访问/修改
// ==========================================================================
typedef struct {
    uint8_t*        data;       /**< 帧数据缓冲区指针（静态内存池分配） */
    uint32_t        width;      /**< 图像宽度 单位：像素 */
    uint32_t        height;     /**< 图像高度 单位：像素 */
    frame_format_t  format;     /**< 像素格式 */
    uint64_t        timestamp;  /**< 采集时间戳(us)  monotonic时钟 */
    uint32_t        index;      /**< 帧序号（自增ID，用于同步/调试） */
    atomic_uint     ref_cnt;    /**< 【私有】原子引用计数，仅API可操作 */
} frame_t;

// ==========================================================================
// @brief 帧链路句柄（对外透明，隐藏内部实现）
// ==========================================================================
typedef struct frame_link_t* frame_link_handle_t;

// ==========================================================================
// @brief 帧链路初始化配置（命名化+内存配置）
// @param   name    帧链路唯一名称（如："main_cam"）
// ==========================================================================
typedef struct {
    const char*     name;               /**< 【必填】帧链路名称 */
    uint32_t        max_frame_size;     /**< 单帧最大字节数 */
    uint32_t        pool_capacity;      /**< 内存池总帧数（推荐4~8） */
    uint32_t        queue_capacity;     /**< 消费队列长度（推荐2~4） */
} frame_link_config_t;

// ==========================================================================
// @name 命名化实例管理API（对标数据总线，无全局变量）
// @brief 用于创建/销毁/查找独立的帧链路实例
// ==========================================================================
/**
 * @brief  通过名称初始化帧链路
 * @param  config  配置参数（名称必填）
 * @return 0成功 负数失败
 */
int frame_link_init(const frame_link_config_t* config);

/**
 * @brief  通过名称销毁帧链路（释放所有内存）
 * @param  name  帧链路名称
 * @return 0成功
 */
int frame_link_deinit(const char* name);

/**
 * @brief  内部通过名称查找帧链路句柄（模块禁止调用）
 * @param  name  帧链路名称
 * @return 句柄 NULL未找到
 */
frame_link_handle_t frame_link_get_handle(const char* name);

// -------------------------------------------------------------------------
// @name 生产者接口（仅采集服务允许调用）
// @brief  用于申请空闲帧、填充数据、入队发布
// -------------------------------------------------------------------------
/**
 * @brief  申请空闲帧（仅分配未被引用的帧）
 * @param  name      帧链路名称
 * @param  out_frame 输出空闲帧指针
 * @return 0成功 池满失败
 */
int frame_link_get_free_frame(const char* name, frame_t** out_frame);

/**
 * @brief  已填充数据的帧入队（队列满自动丢旧帧）
 * @param  name   帧链路名称
 * @param  frame  待发布的帧
 * @return 0成功
 */
int frame_link_enqueue_frame(const char* name, frame_t* frame);

/**
 * @brief  归还未使用的空闲帧到内存池
 * @param  name   帧链路名称
 * @param  frame  待归还的帧
 * @return 0成功
 */
int frame_link_return_free_frame(const char* name, frame_t* frame);

// -------------------------------------------------------------------------
// @name 消费者接口（所有业务服务通用：AI/显示/录像/推流）
// @brief  用于取帧、引用帧、释放帧
// -------------------------------------------------------------------------
/**
 * @brief  非阻塞取出队列中最新帧
 * @param  name      帧链路名称
 * @param  out_frame 输出帧指针
 * @return 0成功 队空失败
 */
int frame_link_dequeue_frame(const char* name, frame_t** out_frame);

/**
 * @brief  帧引用计数+1（使用帧前必须调用）
 * @param  name   帧链路名称
 * @param  frame  待引用的帧
 * @return 0成功
 */
int frame_link_ref_frame(const char* name, frame_t* frame);

/**
 * @brief  帧引用计数-1（使用完成必须调用）
 * @param  name   帧链路名称
 * @param  frame  待释放的帧
 * @return 0成功
 * @note   计数为0时自动归还内存池
 */
int frame_link_unref_frame(const char* name, frame_t* frame);

// -------------------------------------------------------------------------
// @name 监控/调试接口（全模块可调用）
// -------------------------------------------------------------------------
/**
 * @brief  获取队列待消费帧数
 * @param  name  帧链路名称
 * @return 帧数
 */
uint32_t frame_link_get_queue_count(const char* name);

/**
 * @brief  清空消费队列，所有帧归还内存池
 * @param  name  帧链路名称
 * @return 0成功
 */
int frame_link_clear_queue(const char* name);

/**
 * @brief  获取内存池空闲帧数
 * @param  name  帧链路名称
 * @return 空闲帧数
 */
uint32_t frame_link_get_free_count(const char* name);

/**
 * @brief  获取帧当前引用计数（调试专用）
 * @param  frame  帧指针
 * @return 引用计数值
 */
static inline uint32_t frame_link_get_ref_count(frame_t* frame) {
    return atomic_load(&frame->ref_cnt);
}

#ifdef __cplusplus
}
#endif

#endif // __FRAME_LINK_H