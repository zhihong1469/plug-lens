// src/hal/video/inc/video_hal.h
#ifndef VIDEO_HAL_H
#define VIDEO_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ==========================================================================
// 【HAL层铁律】
// 1. 绝对不创建业务线程
// 2. 绝对不碰数据流、业务逻辑
// 3. 锁完全私有，不向上暴露（本层无锁！）
// 4. 不依赖任何上层代码
// 5. 无状态、无全局变量、纯函数操作
// ==========================================================================

// ==========================================================================
// 1. 不透明句柄（状态由Link层管理，HAL层不维护）
// ==========================================================================
typedef void* video_handle_t;

// ==========================================================================
// 2. 通用错误码定义（全硬件兼容）
// ==========================================================================
typedef enum {
    VIDEO_OK = 0,
    VIDEO_ERR_OPEN,
    VIDEO_ERR_QUERYCAP,
    VIDEO_ERR_NOT_CAPTURE,
    VIDEO_ERR_NOT_STREAMING,
    VIDEO_ERR_ENUM_FMT,
    VIDEO_ERR_SET_FMT,
    VIDEO_ERR_REQBUFS,
    VIDEO_ERR_QUERYBUF,
    VIDEO_ERR_MMAP,
    VIDEO_ERR_QBUF,
    VIDEO_ERR_STREAMON,
    VIDEO_ERR_STREAMOFF,
    VIDEO_ERR_POLL,
    VIDEO_ERR_DQBUF,
    VIDEO_ERR_INVALID_PARAM,
    VIDEO_ERR_MUNMAP,
    VIDEO_ERR_CLOSE,
    VIDEO_ERR_SET_FPS,
    VIDEO_ERR_DUMP_FILE
} video_err_t;

// ==========================================================================
// 3. 通用像素格式定义（AI视觉常用）
// ==========================================================================
typedef enum {
    VIDEO_PIX_FMT_YUYV = 0,
    VIDEO_PIX_FMT_NV12,
    VIDEO_PIX_FMT_MJPEG
} video_format_t;

// ==========================================================================
// 4. 摄像头能力检测结果
// ==========================================================================
typedef struct {
    char device_name[32];
    char bus_info[32];
    bool support_yuyv;
    bool support_mjpeg;
    bool support_nv12;
    bool support_manual_exposure;
    bool support_lock_white_balance;
    bool support_lock_gain;
} video_capability_t;

// ==========================================================================
// 5. 通用采集配置
// ==========================================================================
typedef struct {
    const char *dev_path;
    uint32_t width;
    uint32_t height;
    video_format_t format;
    uint32_t fps;
    uint32_t buf_count;
    bool lock_exposure;
    bool lock_white_balance;
    bool lock_gain;
} video_config_t;

// ==========================================================================
// 6. 通用帧数据结构体（零拷贝）
// ==========================================================================
typedef struct {
    void *data;
    uint32_t length;
    uint32_t width;
    uint32_t height;
    video_format_t format;
    uint64_t timestamp;
    uint32_t index;
} video_frame_t;

extern const char* g_err_str[];

// ==========================================================================
// 7. 【核心】完美重构后的通用摄像头接口（无状态、纯函数）
// ==========================================================================

/**
 * @brief 打开摄像头设备（无状态，返回句柄）
 * @param config 采集配置
 * @param cap 输出：摄像头能力检测结果
 * @param out_handle 输出：不透明句柄
 * @return 错误码
 */
video_err_t video_open(const video_config_t *config,
                       video_capability_t *cap,
                       video_handle_t *out_handle);

/**
 * @brief 关闭摄像头设备（释放所有硬件资源）
 * @param handle 句柄
 * @return 错误码
 */
video_err_t video_close(video_handle_t handle);

/**
 * @brief 启动视频流
 * @param handle 句柄
 * @return 错误码
 */
video_err_t video_start_stream(video_handle_t handle);

/**
 * @brief 停止视频流
 * @param handle 句柄
 * @return 错误码
 */
video_err_t video_stop_stream(video_handle_t handle);

/**
 * @brief 获取一帧数据（无锁、阻塞、纯硬件操作）
 * @param handle 句柄
 * @param frame 输出帧
 * @return 错误码
 * @note 【重要】必须调用 video_put_frame() 归还缓冲区！
 */
video_err_t video_get_frame(video_handle_t handle, video_frame_t *frame);

/**
 * @brief 归还帧缓冲区（无锁、纯硬件操作）
 * @param handle 句柄
 * @param frame 帧
 * @return 错误码
 */
video_err_t video_put_frame(video_handle_t handle, const video_frame_t *frame);

/**
 * @brief 动态设置帧率（需在STOP状态下调用）
 * @param handle 句柄
 * @param fps 期望帧率
 * @return 错误码
 */
video_err_t video_set_fps(video_handle_t handle, uint32_t fps);

/**
 * @brief 枚举指定格式下支持的所有分辨率
 */
uint32_t video_enum_sizes(video_handle_t handle,
                           video_format_t fmt,
                           uint32_t (*sizes)[2],
                           uint32_t max_cnt);

/**
 * @brief 枚举指定格式和分辨率下支持的帧率
 */
uint32_t video_enum_fps(video_handle_t handle,
                         video_format_t fmt,
                         uint32_t width,
                         uint32_t height,
                         uint32_t *fps,
                         uint32_t max_cnt);

/**
 * @brief 保存帧为YUYV裸数据文件（调试用）
 */
video_err_t video_dump_yuv(const video_frame_t *frame, const char *filepath);

/**
 * @brief 获取错误描述字符串
 * @param err 错误码
 * @return 错误描述（永远不为NULL）
 */
const char* video_err_str(video_err_t err);

#endif /* VIDEO_HAL_H */
