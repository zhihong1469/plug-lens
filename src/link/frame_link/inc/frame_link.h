/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           frame_link.h
 * @brief          FrameLink 多实例数据链路层 对外接口
 * @defgroup       FrameLink
 * @details        命名化帧内存池 | 原子引用计数 | 单写多读 | 零拷贝
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

/* ========================== 配置宏定义 ========================== */
#define FRAME_LINK_NAME_MAX_LEN        32      /**< 链路名称最大长度 */
#define FRAME_LINK_MAX_INSTANCES       4       /**< 最大实例数 */
#define FRAME_LINK_POOL_MIN            4       /**< 内存池最小帧数 */
#define FRAME_LINK_POOL_MAX            8       /**< 内存池最大帧数 */
#define FRAME_LINK_LOCK_TIMEOUT_MS     10      /**< 互斥锁超时时间 */

/* ========================== 错误码 ========================== */
typedef enum {
    FL_OK               = 0,
    FL_INVALID_PARAM    = -1,
    FL_NO_MEM           = -2,
    FL_NOT_FOUND        = -3,
    FL_BUSY             = -4,
    FL_TIMEOUT          = -5,
    FL_NO_FREE_FRAME    = -6,
} fl_err_t;

/* ========================== 帧格式 ========================== */
typedef enum {
    FRAME_FMT_YUYV      = 0,
    FRAME_FMT_NV12      = 1,
    FRAME_FMT_MJPEG     = 2,
} frame_format_t;

/* ========================== 消费模式 ========================== */
typedef enum {
    FL_CONSUME_LATEST   = 0,    /**< 获取最新帧 */
    FL_CONSUME_QUEUE    = 1,    /**< 从队列取帧 */
} fl_consume_mode_t;

/* ========================== 数据类型 ========================== */
/** 帧信息结构体（对外可见） */
typedef struct {
    uint32_t            width;
    uint32_t            height;
    frame_format_t      format;
    uint32_t            data_size;
    uint64_t            timestamp_us;
    uint32_t            frame_id;
} frame_info_t;

/** 不透明句柄定义 */
typedef struct frame_link *frame_link_handle_t;
typedef struct frame *frame_handle_t;

/* ========================== 链路初始化配置 ========================== */
typedef struct {
    char                name[FRAME_LINK_NAME_MAX_LEN];
    uint32_t            max_frame_size;
    uint32_t            pool_count;
    uint32_t            queue_count;
} frame_link_cfg_t;

/* ========================== 全局接口 ========================== */
/**
 * @brief  创建命名帧链路
 * @param  cfg: 配置参数
 * @return 错误码
 */
fl_err_t frame_link_create(const frame_link_cfg_t *cfg);

/**
 * @brief  销毁命名帧链路
 * @param  name: 链路名称
 * @return 错误码
 */
fl_err_t frame_link_destroy(const char *name);

/**
 * @brief  清空链路所有帧资源
 * @param  name: 链路名称
 * @return 错误码
 */
fl_err_t frame_link_clear(const char *name);

/* ========================== 生产者接口 ========================== */
/**
 * @brief  生产者获取空闲帧
 * @param  name: 链路名称
 * @param  out_frame: 输出帧句柄
 * @return 错误码
 */
fl_err_t frame_link_producer_get(const char *name, frame_handle_t *out_frame);

/**
 * @brief  生产者推送帧到链路
 * @param  name: 链路名称
 * @param  frame: 帧句柄
 * @return 错误码
 */
fl_err_t frame_link_producer_push(const char *name, frame_handle_t frame);

/**
 * @brief  获取帧可写指针（仅生产者可用）
 * @param  frame: 帧句柄
 * @return 数据指针 / NULL
 */
void *frame_get_writable_ptr(frame_handle_t frame);

/* ========================== 消费者接口 ========================== */
/**
 * @brief  通过DataBus句柄绑定帧（持证取帧）
 * @param  name: 链路名称
 * @param  bus_frame: 总线传递的帧句柄
 * @param  out_frame: 输出绑定后的帧
 * @return 错误码
 */
fl_err_t frame_link_consumer_get_by_bus(const char *name, frame_handle_t bus_frame, frame_handle_t *out_frame);

/**
 * @brief  消费者直接获取帧
 * @param  name: 链路名称
 * @param  mode: 消费模式
 * @param  out_frame: 输出帧句柄
 * @return 错误码
 */
fl_err_t frame_link_consumer_get(const char *name, fl_consume_mode_t mode, frame_handle_t *out_frame);

/**
 * @brief  消费者释放帧引用
 * @param  frame: 帧句柄
 * @return 错误码
 */
fl_err_t frame_link_consumer_put(frame_handle_t frame);

/**
 * @brief  获取帧只读指针（消费者专用）
 * @param  frame: 帧句柄
 * @return 只读数据指针
 */
const void *frame_get_readonly_ptr(frame_handle_t frame);

/**
 * @brief  获取帧元数据
 * @param  frame: 帧句柄
 * @param  info: 输出信息
 * @return 错误码
 */
fl_err_t frame_get_info(frame_handle_t frame, frame_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* __FRAME_LINK_H */