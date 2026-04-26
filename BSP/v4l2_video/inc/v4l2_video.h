#ifndef __V4L2_VIDEO_H
#define __V4L2_VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ==========================================================================
// 1. 错误码定义（完善、可追溯）
// ==========================================================================
typedef enum {
    V4L2_VIDEO_OK = 0,
    V4L2_VIDEO_ERR_OPEN,
    V4L2_VIDEO_ERR_QUERYCAP,
    V4L2_VIDEO_ERR_NOT_CAPTURE,
    V4L2_VIDEO_ERR_NOT_STREAMING,
    V4L2_VIDEO_ERR_ENUM_FMT,
    V4L2_VIDEO_ERR_SET_FMT,
    V4L2_VIDEO_ERR_REQBUFS,
    V4L2_VIDEO_ERR_QUERYBUF,
    V4L2_VIDEO_ERR_MMAP,
    V4L2_VIDEO_ERR_QBUF,
    V4L2_VIDEO_ERR_STREAMON,
    V4L2_VIDEO_ERR_STREAMOFF,
    V4L2_VIDEO_ERR_POLL,
    V4L2_VIDEO_ERR_DQBUF,
    V4L2_VIDEO_ERR_LOCK,
    V4L2_VIDEO_ERR_UNLOCK,
    V4L2_VIDEO_ERR_INVALID_PARAM,
    V4L2_VIDEO_ERR_NOT_INIT,
    V4L2_VIDEO_ERR_ALREADY_INIT,
    V4L2_VIDEO_ERR_MUNMAP,
    V4L2_VIDEO_ERR_CLOSE
} v4l2_video_err_t;

// ==========================================================================
// 2. 像素格式定义（AI视觉常用）
// ==========================================================================
typedef enum {
    V4L2_PIX_FMT_YUYV = 0,  // 默认：AI首选无压缩4:2:2格式
    V4L2_PIX_FMT_NV12,       // 备选：部分NPU/CNN偏好的4:2:0格式
    V4L2_PIX_FMT_MJPEG       // 预留：仅用于特殊高带宽场景
} v4l2_video_format_t;

// ==========================================================================
// 3. 采集配置结构体（AI专用优化）
// ==========================================================================
typedef struct {
    const char *dev_path;        // 摄像头设备路径（如 "/dev/video0"）
    uint32_t width;               // 期望采集宽度（如 640/1280）
    uint32_t height;              // 期望采集高度（如 480/720）
    v4l2_video_format_t format;   // 像素格式
    uint32_t fps;                 // 期望采集帧率（如 15/30）
    uint32_t buf_count;           // 内核缓冲区数量（建议 3-5）
    
    // AI推理稳定性专用配置（默认全锁）
    bool lock_exposure;           // 锁定自动曝光（防止画面忽明忽暗）
    bool lock_white_balance;      // 锁定自动白平衡（防止颜色漂移）
    bool lock_gain;               // 锁定自动增益（防止噪点波动）
} v4l2_video_config_t;

// ==========================================================================
// 4. 帧数据结构体（零拷贝输出给AI）
// ==========================================================================
typedef struct {
    void *data;          // 帧数据指针（直接指向内核MMAP缓冲区，零拷贝）
    uint32_t length;     // 帧数据总长度（字节）
    uint32_t width;      // 实际宽度（驱动可能会调整，以此为准）
    uint32_t height;     // 实际高度（驱动可能会调整，以此为准）
    v4l2_video_format_t format; // 实际像素格式
    uint64_t timestamp;  // 帧时间戳（微秒，用于AI多模态同步）
    uint32_t index;      // 内部缓冲区索引（用户无需关心）
} v4l2_video_frame_t;

// ==========================================================================
// 5. 对外API接口（极简、通用、线程安全）
// ==========================================================================

/**
 * @brief 初始化V4L2视频采集模块
 * @param config 采集配置结构体指针（不能为空）
 * @return 错误码（V4L2_VIDEO_OK 表示成功）
 */
v4l2_video_err_t v4l2_video_init(const v4l2_video_config_t *config);

/**
 * @brief 启动视频采集流
 * @return 错误码
 */
v4l2_video_err_t v4l2_video_start(void);

/**
 * @brief 获取一帧原始数据（线程安全，阻塞等待）
 * @param frame 输出帧结构体指针（不能为空）
 * @return 错误码
 * @note 【重要】调用后必须调用 v4l2_video_put_frame() 归还缓冲区！
 *       否则会导致缓冲区耗尽，采集卡死！
 */
v4l2_video_err_t v4l2_video_get_frame(v4l2_video_frame_t *frame);

/**
 * @brief 归还帧缓冲区给驱动（必须与get_frame严格配对调用）
 * @param frame 帧结构体指针（不能为空）
 * @return 错误码
 */
v4l2_video_err_t v4l2_video_put_frame(const v4l2_video_frame_t *frame);

/**
 * @brief 停止视频采集流
 * @return 错误码
 */
v4l2_video_err_t v4l2_video_stop(void);

/**
 * @brief 反初始化模块，释放所有资源（必须调用！）
 * @return 错误码
 */
v4l2_video_err_t v4l2_video_deinit(void);

/**
 * @brief 获取错误描述字符串（用于调试/日志）
 * @param err 错误码
 * @return 错误描述字符串（永远不为NULL）
 */
const char* v4l2_video_err_str(v4l2_video_err_t err);

#endif /* __V4L2_VIDEO_H */
