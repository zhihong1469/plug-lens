// plugins/hal_plugins/video_usb/inc/video_usb.h
#ifndef VIDEO_USB_H
#define VIDEO_USB_H

#include "video_hal.h"
#include <linux/videodev2.h>

#define VIDEO_USB_MAX_BUFS 32

// 【核心】内部状态结构体（完全封装，不向上暴露）
typedef struct {
    int fd;
    video_config_t config;
    video_capability_t cap;
    void *buffers[VIDEO_USB_MAX_BUFS];
    uint32_t buffer_lengths[VIDEO_USB_MAX_BUFS];
    uint32_t buf_count;
} video_usb_context_t;

// 纯硬件实现内部函数（接收handle参数）
int  _video_usb_open(const char *dev_path, video_usb_context_t **ctx);
void _video_usb_close(video_usb_context_t *ctx);
int  _video_usb_set_format(video_usb_context_t *ctx);
int  _video_usb_set_fps(video_usb_context_t *ctx, uint32_t fps);
int  _video_usb_alloc_buffers(video_usb_context_t *ctx);
int  _video_usb_mmap_buffers(video_usb_context_t *ctx);
int  _video_usb_queue_all_buffers(video_usb_context_t *ctx);
int  _video_usb_streamon(video_usb_context_t *ctx);
int  _video_usb_streamoff(video_usb_context_t *ctx);
int  _video_usb_dqbuf(video_usb_context_t *ctx, video_frame_t *frame);
int  _video_usb_qbuf(video_usb_context_t *ctx, uint32_t index);
int  _video_usb_lock_ai_params(video_usb_context_t *ctx);
int  _video_usb_detect_capability(video_usb_context_t *ctx);
int  _video_usb_enum_sizes(video_usb_context_t *ctx, video_format_t fmt, uint32_t (*sizes)[2], uint32_t max_cnt);
int  _video_usb_enum_fps(video_usb_context_t *ctx, video_format_t fmt, uint32_t w, uint32_t h, uint32_t *fps, uint32_t max_cnt);
int  _video_usb_dump_yuv(const video_frame_t *frame, const char *path);
int _video_usb_get_fd(video_usb_context_t *ctx);
#endif /* VIDEO_USB_H */