#ifndef __V4L2_APP_H
#define __V4L2_APP_H

#include "v4l2_video.h"
#include "queue.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// APP 层配置结构体
// ==========================================================================
typedef struct {
    v4l2_video_config_t v4l2_cfg;   // 底层 V4L2 配置（直接透传给驱动层）
    uint32_t queue_size;              // 环形队列缓存帧数（建议 3-5，需 <= V4L2 buf_count）
} v4l2_app_config_t;

// ==========================================================================
// 对外 API 接口
// ==========================================================================

/**
 * @brief 初始化 V4L2 应用层
 * @param config 配置结构体指针（不能为空）
 * @return 0 成功，-1 失败
 * @note 内部会自动完成：1. 初始化 V4L2 驱动 2. 初始化帧池 3. 初始化环形队列
 */
int v4l2_app_init(const v4l2_app_config_t *config);

/**
 * @brief 启动视频采集（内部创建采集线程）
 * @return 0 成功，-1 失败
 */
int v4l2_app_start(void);

/**
 * @brief 获取一帧数据（消费者接口，从环形队列取）
 * @param frame      输出参数，返回帧数据指针的地址
 * @param timeout_ms 超时时间（毫秒）：0=非阻塞，-1=无限阻塞，>0=等待指定时间
 * @return 0 成功获取，-1 超时/失败
 * @note 【重要】获取后必须调用 v4l2_app_release_frame() 归还！否则会导致资源泄漏！
 */
int v4l2_app_get_frame(v4l2_video_frame_t **frame, int timeout_ms);

/**
 * @brief 归还帧数据（消费者接口，释放回驱动和帧池）
 * @param frame 帧数据指针（由 get_frame 获取）
 */
void v4l2_app_release_frame(v4l2_video_frame_t *frame);

/**
 * @brief 停止视频采集（停止采集线程）
 * @return 0 成功，-1 失败
 */
int v4l2_app_stop(void);

/**
 * @brief 反初始化，释放所有资源（必须在 stop 之后调用）
 */
void v4l2_app_deinit(void);

// ==========================================================================
// 调试工具接口
// ==========================================================================

/**
 * @brief 保存一帧为 YUYV 裸数据文件（应用层调试用）
 * @param frame    帧数据指针
 * @param save_dir 保存目录（如 "/tmp"，文件会自动命名为 frame_xxxxxx.yuv）
 * @return 0 成功，-1 失败
 */
int v4l2_app_save_yuv(const v4l2_video_frame_t *frame, const char *save_dir);

#endif /* __V4L2_APP_H */
