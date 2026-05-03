// plugins/hal_plugins/video_usb/src/video_hal.c
#include "video_hal.h"
#include "video_usb.h"
#include <string.h>
#include <stdlib.h>

// 错误码字符串表（纯数据，无状态）
const char* g_err_str[] = {
    [VIDEO_OK] = "Success",
    [VIDEO_ERR_OPEN] = "Failed to open device",
    [VIDEO_ERR_QUERYCAP] = "Failed to query device capabilities",
    [VIDEO_ERR_NOT_CAPTURE] = "Device does not support video capture",
    [VIDEO_ERR_NOT_STREAMING] = "Device does not support streaming I/O",
    [VIDEO_ERR_ENUM_FMT] = "Failed to enumerate formats",
    [VIDEO_ERR_SET_FMT] = "Failed to set format",
    [VIDEO_ERR_SET_FPS] = "Failed to set fps",
    [VIDEO_ERR_REQBUFS] = "Failed to request buffers",
    [VIDEO_ERR_QUERYBUF] = "Failed to query buffer",
    [VIDEO_ERR_MMAP] = "Failed to mmap buffer",
    [VIDEO_ERR_QBUF] = "Failed to queue buffer",
    [VIDEO_ERR_STREAMON] = "Failed to start stream",
    [VIDEO_ERR_STREAMOFF] = "Failed to stop stream",
    [VIDEO_ERR_POLL] = "Poll failed or timeout",
    [VIDEO_ERR_DQBUF] = "Failed to dequeue buffer",
    [VIDEO_ERR_INVALID_PARAM] = "Invalid parameter",
    [VIDEO_ERR_MUNMAP] = "Failed to munmap buffer",
    [VIDEO_ERR_CLOSE] = "Failed to close device",
    [VIDEO_ERR_DUMP_FILE] = "Failed to dump file"
};

const char* video_err_str(video_err_t err)
{
    if (err < 0 || err >= sizeof(g_err_str) / sizeof(g_err_str[0])) {
        return "Unknown error";
    }
    return g_err_str[err];
}
#include "log.h"
video_err_t video_open(const video_config_t *config,
                       video_capability_t *cap,
                       video_handle_t *out_handle)
{
    if (config == NULL || config->dev_path == NULL || cap == NULL || out_handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }

    video_usb_context_t *ctx = NULL;
    int ret = _video_usb_open(config->dev_path, &ctx);
    if (ret != 0) {
        return VIDEO_ERR_OPEN;
    }

    // 拷贝配置
    memcpy(&ctx->config, config, sizeof(video_config_t));
    if (ctx->config.buf_count == 0) ctx->config.buf_count = 4;
    if (ctx->config.buf_count > VIDEO_USB_MAX_BUFS) ctx->config.buf_count = VIDEO_USB_MAX_BUFS;

    // 检测能力
    if (_video_usb_detect_capability(ctx) != 0) {
        // 非致命错误，继续
    }
    memcpy(cap, &ctx->cap, sizeof(video_capability_t));
    // 【新增】打印摄像头能力
    LOG_I("Video HAL: Detected camera: %s", ctx->cap.device_name);
    LOG_I("Video HAL: Support YUYV: %s", ctx->cap.support_yuyv ? "YES" : "NO");
    LOG_I("Video HAL: Support MJPEG: %s", ctx->cap.support_mjpeg ? "YES" : "NO");
    LOG_I("Video HAL: Support NV12: %s", ctx->cap.support_nv12 ? "YES" : "NO");
    // 设置格式
    if (_video_usb_set_format(ctx) != 0) {
        _video_usb_close(ctx);
        return VIDEO_ERR_SET_FMT;
    }

    // 设置帧率
    if (ctx->config.fps > 0) {
        if (_video_usb_set_fps(ctx, ctx->config.fps) != 0) {
            // 非致命错误，继续
        }
    }

    // 分配缓冲区
    if (_video_usb_alloc_buffers(ctx) != 0) {
        _video_usb_close(ctx);
        return VIDEO_ERR_REQBUFS;
    }

    // MMAP
    if (_video_usb_mmap_buffers(ctx) != 0) {
        _video_usb_close(ctx);
        return VIDEO_ERR_MMAP;
    }

    // 锁定AI参数
    if (ctx->config.lock_exposure || ctx->config.lock_white_balance || ctx->config.lock_gain) {
        (void)_video_usb_lock_ai_params(ctx);
    }

    // 返回句柄
    *out_handle = (video_handle_t)ctx;
    return VIDEO_OK;
}

video_err_t video_close(video_handle_t handle)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    _video_usb_close((video_usb_context_t*)handle);
    return VIDEO_OK;
}

video_err_t video_start_stream(video_handle_t handle)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    video_usb_context_t *ctx = (video_usb_context_t*)handle;
    
    if (_video_usb_queue_all_buffers(ctx) != 0) {
        return VIDEO_ERR_QBUF;
    }
    if (_video_usb_streamon(ctx) != 0) {
        return VIDEO_ERR_STREAMON;
    }
    return VIDEO_OK;
}

video_err_t video_stop_stream(video_handle_t handle)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    if (_video_usb_streamoff((video_usb_context_t*)handle) != 0) {
        return VIDEO_ERR_STREAMOFF;
    }
    return VIDEO_OK;
}

video_err_t video_get_frame(video_handle_t handle, video_frame_t *frame)
{
    if (handle == NULL || frame == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    video_usb_context_t *ctx = (video_usb_context_t*)handle;
    
    if (_video_usb_dqbuf(ctx, frame) != 0) {
        return VIDEO_ERR_DQBUF;
    }
    
    // 填充完整信息
    frame->width = ctx->config.width;
    frame->height = ctx->config.height;
    frame->format = ctx->config.format;
    
    return VIDEO_OK;
}

video_err_t video_put_frame(video_handle_t handle, const video_frame_t *frame)
{
    if (handle == NULL || frame == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    if (_video_usb_qbuf((video_usb_context_t*)handle, frame->index) != 0) {
        return VIDEO_ERR_QBUF;
    }
    return VIDEO_OK;
}

video_err_t video_set_fps(video_handle_t handle, uint32_t fps)
{
    if (handle == NULL || fps == 0) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    video_usb_context_t *ctx = (video_usb_context_t*)handle;
    if (_video_usb_set_fps(ctx, fps) != 0) {
        return VIDEO_ERR_SET_FPS;
    }
    ctx->config.fps = fps;
    return VIDEO_OK;
}

uint32_t video_enum_sizes(video_handle_t handle,
                           video_format_t fmt,
                           uint32_t (*sizes)[2],
                           uint32_t max_cnt)
{
    if (handle == NULL || sizes == NULL || max_cnt == 0) {
        return 0;
    }
    return _video_usb_enum_sizes((video_usb_context_t*)handle, fmt, sizes, max_cnt);
}

uint32_t video_enum_fps(video_handle_t handle,
                         video_format_t fmt,
                         uint32_t width,
                         uint32_t height,
                         uint32_t *fps,
                         uint32_t max_cnt)
{
    if (handle == NULL || fps == NULL || max_cnt == 0) {
        return 0;
    }
    return _video_usb_enum_fps((video_usb_context_t*)handle, fmt, width, height, fps, max_cnt);
}

video_err_t video_dump_yuv(const video_frame_t *frame, const char *filepath)
{
    if (frame == NULL || filepath == NULL || frame->data == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    if (_video_usb_dump_yuv(frame, filepath) != 0) {
        return VIDEO_ERR_DUMP_FILE;
    }
    return VIDEO_OK;
}

// 新增：HAL层转发获取摄像头fd
int video_get_wait_fd(video_handle_t handle)
{
    if (handle == NULL) return -1;
    video_usb_context_t *ctx = (video_usb_context_t*)handle;
    return _video_usb_get_fd(ctx);
}