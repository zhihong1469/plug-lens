#ifndef __FRAME_LINK_H
#define __FRAME_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "pool.h"
#include "queue.h"
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 帧格式定义（和摄像头/AI通用）
// ==========================================================================
typedef enum {
    FRAME_FMT_YUYV = 0,
    FRAME_FMT_NV12,
    FRAME_FMT_RGB888,
    FRAME_FMT_BGR888
} frame_format_t;

// ==========================================================================
// 通用帧结构体（全系统唯一帧格式）
// ==========================================================================
typedef struct {
    uint8_t*        data;       // 帧数据指针
    uint32_t        width;      // 宽度
    uint32_t        height;     // 高度
    frame_format_t  format;     // 像素格式
    uint64_t        timestamp;  // 时间戳（微秒）
    uint32_t        index;      // 帧序号
} frame_t;

// ==========================================================================
// 不透明句柄（对外隐藏内部实现）
// ==========================================================================
typedef void* frame_link_handle_t;

// ==========================================================================
// FrameLink 配置
// ==========================================================================
typedef struct {
    uint32_t max_frame_size;    // 单帧最大字节数（如640x480 YUYV=614400）
    uint32_t pool_capacity;     // 内存池总帧数（建议4-8）
    uint32_t queue_capacity;    // 队列最大长度（建议2-4，丢旧保新）
} frame_link_config_t;

// ==========================================================================
// 对外核心API（纯数据管道，无任何业务逻辑）
// ==========================================================================

/**
 * @brief 初始化帧链路
 * @param config 配置参数
 * @param out_handle 输出句柄
 * @return 0成功，负数失败
 */
int frame_link_init(const frame_link_config_t* config, frame_link_handle_t* out_handle);

/**
 * @brief 销毁帧链路，释放所有资源
 * @param handle 句柄
 * @return 0成功
 */
int frame_link_deinit(frame_link_handle_t handle);

// -------------------------------------------------------------------------
// 生产者接口（采集服务专用）
// -------------------------------------------------------------------------

/**
 * @brief 从内存池获取一个空闲帧
 * @param handle 句柄
 * @param out_frame 输出帧指针
 * @return 0成功，POOL_ERR_EMPTY池空
 */
int frame_link_get_free_frame(frame_link_handle_t handle, frame_t** out_frame);

/**
 * @brief 将填充好的帧入队（自动丢旧保新）
 * @param handle 句柄
 * @param frame 帧指针
 * @return 0成功
 * @note 队列满时自动丢弃最旧帧，保证永远保留最新帧
 */
int frame_link_enqueue_frame(frame_link_handle_t handle, frame_t* frame);

/**
 * @brief 归还未使用的空闲帧到内存池
 * @param handle 句柄
 * @param frame 帧指针
 * @return 0成功
 */
int frame_link_return_free_frame(frame_link_handle_t handle, frame_t* frame);

// -------------------------------------------------------------------------
// 消费者接口（AI/显示/录像服务专用）
// -------------------------------------------------------------------------

/**
 * @brief 从队列取出一帧（非阻塞）
 * @param handle 句柄
 * @param out_frame 输出帧指针
 * @return 0成功，QUEUE_ERR_EMPTY队空
 */
int frame_link_dequeue_frame(frame_link_handle_t handle, frame_t** out_frame);

/**
 * @brief 消费完成后归还帧到内存池
 * @param handle 句柄
 * @param frame 帧指针
 * @return 0成功
 */
int frame_link_release_frame(frame_link_handle_t handle, frame_t* frame);

// -------------------------------------------------------------------------
// 管理接口
// -------------------------------------------------------------------------

/**
 * @brief 获取当前队列中待消费的帧数
 * @param handle 句柄
 * @return 帧数
 */
uint32_t frame_link_get_queue_count(frame_link_handle_t handle);

/**
 * @brief 清空队列，归还所有帧到内存池
 * @param handle 句柄
 * @return 0成功
 */
int frame_link_clear_queue(frame_link_handle_t handle);

/**
 * @brief 获取当前内存池空闲帧数
 * @param handle 句柄
 * @return 空闲帧数
 */
uint32_t frame_link_get_free_count(frame_link_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // __FRAME_LINK_H