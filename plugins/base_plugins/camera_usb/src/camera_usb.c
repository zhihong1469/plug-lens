/**
 * @file    camera_usb.c
 * @brief   USB Camera Subclass Driver Implementation
 * @details Low-level V4L2 implementation for USB camera:
 *          - Full self-inspection during initialization
 *          - MMAP buffer management for zero-copy capture
 *          - Support YUYV/MJPEG/NV12 formats
 *          - Parameter control: exposure, brightness, white balance
 *          - Inherits camera_base OOP interface
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "../inc/camera_usb.h"
#include "../inc/camera_base.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "vision_ai_config.h"

/** Maximum number of USB camera DMA buffers (configured in project) */
#define CAM_USB_MAX_BUF             CONFIG_CAPTURE_BUF_COUNT

/**
 * @brief   Private USB camera subclass structure (fully encapsulated)
 * @details V3.0 Rule: Base class MUST be the first member for OOP inheritance.
 *          All members are private, inaccessible to upper-layer code.
 * @note    Use container_of() to convert base pointer to subclass pointer.
 */
typedef struct {
    camera_base_t        base;                           /**< Camera base class (MUST be first) */
    camera_capability_t  cap;                            /**< Camera hardware capability set */
    void                *buf[CONFIG_CAPTURE_BUF_COUNT];  /**< MMAP mapped buffers */
    size_t               buf_len[CONFIG_CAPTURE_BUF_COUNT];/**< Buffer length */
    const char          *dev_path;                       /**< V4L2 device node path */
    uint32_t             pixel_fmt;                      /**< Pixel format (YUYV/MJPEG/NV12) */
    int                  fd;                             /**< V4L2 file descriptor */
    int                  buf_cnt;                        /**< Actual used buffer count */
} camera_usb_t;

/* ============================================================================
 * Private Helper Functions
 * ========================================================================== */
/**
 * @brief   Check if target pixel format is supported by camera hardware
 * @param   me  Pointer to USB camera private instance
 * @return  0 on success; -ENOTSUP if format not supported
 */
static int camera_usb_check_format_support(camera_usb_t *me)
{
    if (!me->cap.support_yuyv && me->pixel_fmt == V4L2_PIX_FMT_YUYV) {
        printf("[USB Camera] YUYV format not supported\n");
        return -ENOTSUP;
    }
    if (!me->cap.support_mjpeg && me->pixel_fmt == V4L2_PIX_FMT_MJPEG) {
        printf("[USB Camera] MJPEG format not supported\n");
        return -ENOTSUP;
    }
    if (!me->cap.support_nv12 && me->pixel_fmt == V4L2_PIX_FMT_NV12) {
        printf("[USB Camera] NV12 format not supported\n");
        return -ENOTSUP;
    }
    return 0;
}

/* ============================================================================
 * Subclass Virtual Function Implementations (OOP Interface)
 * ========================================================================== */
/**
 * @copydoc camera_base_init
 */
static int camera_usb_init(camera_base_t *base_me)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    int ret, i;

    // Step 1: Open V4L2 device
    me->fd = v4l2_open(me->dev_path);
    if (me->fd < 0) {
        perror("[USB Camera] open failed");
        return -errno;
    }

    // Step 2: Query device basic capabilities
    ret = v4l2_query_capability(me->fd, &me->cap);
    if (ret < 0) {
        printf("[USB Camera] Failed to query device capability\n");
        goto err_close;
    }

    // Step 3: Enumerate all supported formats
    ret = v4l2_enum_formats(me->fd, &me->cap);
    if (ret < 0) {
        printf("[USB Camera] Failed to enumerate formats\n");
        goto err_close;
    }

    // Step 4: Validate target pixel format
    ret = camera_usb_check_format_support(me);
    if (ret < 0)
        goto err_close;

    // Step 5: Set and verify image format
    int w = me->base.width;
    int h = me->base.height;
    ret = v4l2_set_format(me->fd, &w, &h, me->pixel_fmt);
    if (ret < 0) {
        perror("[USB Camera] set format failed");
        goto err_close;
    }
    me->base.width = w;
    me->base.height = h;

    // Step 6: Set and read back frame rate
    uint32_t fps = me->base.fps;
    ret = v4l2_set_fps(me->fd, &fps);
    if (ret < 0) {
        printf("[USB Camera] Warning: set FPS failed\n");
    }
    me->base.fps = fps;

    // Step 7: Request DMA buffers from kernel
    me->buf_cnt = CAM_USB_MAX_BUF;
    ret = v4l2_reqbufs(me->fd, &me->buf_cnt);
    if (ret < 0) {
        perror("[USB Camera] reqbufs failed");
        goto err_close;
    }

    // Step 8: MMAP mapping and buffer queue
    for (i = 0; i < me->buf_cnt; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        ret = v4l2_querybuf(me->fd, &buf);
        if (ret < 0) {
            perror("[USB Camera] querybuf failed");
            goto err_munmap;
        }

        me->buf[i] = v4l2_mmap(me->fd, buf.length, buf.m.offset);
        me->buf_len[i] = buf.length;

        if (me->buf[i] == MAP_FAILED) {
            perror("[USB Camera] mmap failed");
            goto err_munmap;
        }

        // Queue buffer to kernel
        ret = v4l2_qbuf(me->fd, &buf);
        if (ret < 0) {
            perror("[USB Camera] qbuf failed");
            goto err_munmap;
        }
    }

    printf("[USB Camera] Initialization self-check passed ✅\n");
    return 0;

err_munmap:
    // Unmap allocated buffers on failure
    for (int j = 0; j < i; j++)
        v4l2_munmap(me->buf[j], me->buf_len[j]);
err_close:
    v4l2_close(me->fd);
    me->fd = -1;
    return ret;
}

/**
 * @copydoc camera_base_deinit
 */
static int camera_usb_deinit(camera_base_t *base_me)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);

    // Stop capture before releasing resources
    camera_stop_capture(base_me);

    // Unmap all DMA buffers
    for (int i = 0; i < me->buf_cnt; i++)
        v4l2_munmap(me->buf[i], me->buf_len[i]);

    // Close V4L2 device
    v4l2_close(me->fd);
    me->fd = -1;

    printf("[USB Camera] De-initialization completed\n");
    return 0;
}

/**
 * @copydoc camera_base_start_capture
 */
static int camera_usb_start_capture(camera_base_t *base_me)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    if (me->fd < 0) return -ENODEV;

    // Start V4L2 video stream
    int ret = v4l2_stream_ctrl(me->fd, true);
    if (ret < 0) {
        perror("[USB Camera] stream on failed");
        return ret;
    }

    printf("[USB Camera] Start capture\n");
    return 0;
}

/**
 * @copydoc camera_base_stop_capture
 */
static int camera_usb_stop_capture(camera_base_t *base_me)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    if (me->fd < 0) return -ENODEV;

    // Stop V4L2 video stream
    v4l2_stream_ctrl(me->fd, false);
    printf("[USB Camera] Stop capture\n");
    return 0;
}

/**
 * @copydoc camera_base_get_frame
 */
static int camera_usb_get_frame(camera_base_t *base_me, void **frame, size_t *len)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    if (me->fd < 0 || !base_me->is_running) return -ENODEV;

    struct v4l2_buffer buf = {0};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // Dequeue filled buffer from kernel
    int ret = v4l2_dqbuf(me->fd, &buf);
    if (ret < 0) return ret;

    // Return frame data pointer and length (zero-copy)
    *frame = me->buf[buf.index];
    *len = me->buf_len[buf.index];

    // Requeue buffer to kernel
    v4l2_qbuf(me->fd, &buf);
    return 0;
}

/**
 * @copydoc camera_base_set_param
 */
static int camera_usb_set_param(camera_base_t *base_me, int cmd, void *arg)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    if (me->fd < 0 || !arg) return -EINVAL;

    int *val = (int *)arg;
    int ret;

    switch (cmd) {
        case CAMERA_PARAM_SET_EXPOSURE:
            if (!me->cap.support_exposure) return -ENOTSUP;
            v4l2_set_ctrl(me->fd, V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL);
            ret = v4l2_set_ctrl(me->fd, V4L2_CID_EXPOSURE_ABSOLUTE, *val);
            printf("[USB Camera] Exposure = %d\n", *val);
            return ret;

        case CAMERA_PARAM_SET_BRIGHTNESS:
            ret = v4l2_set_ctrl(me->fd, V4L2_CID_BRIGHTNESS, *val);
            printf("[USB Camera] Brightness = %d\n", *val);
            return ret;

        case CAMERA_PARAM_SET_WHITE_BALANCE:
            if (!me->cap.support_white_balance) return -ENOTSUP;
            v4l2_set_ctrl(me->fd, V4L2_CID_AUTO_WHITE_BALANCE, 0);
            ret = v4l2_set_ctrl(me->fd, V4L2_CID_WHITE_BALANCE_TEMPERATURE, *val);
            printf("[USB Camera] White Balance = %d\n", *val);
            return ret;

        default:
            return -ENOSYS;
    }
}

/**
 * @copydoc camera_base_get_capability
 */
static int camera_usb_get_capability(camera_base_t *base_me, camera_capability_t *cap)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    memcpy(cap, &me->cap, sizeof(*cap));
    return 0;
}

/* ============================================================================
 * Camera Subclass Virtual Function Table (CONST required by V3.0)
 * ========================================================================== */
/**
 * @brief   Virtual function table for USB camera subclass
 * @details Implements all mandatory interfaces from camera_base_t.
 *          Binds subclass functions to base class callbacks.
 */
static const camera_ops_t g_camera_usb_ops = {
    .init           = camera_usb_init,
    .deinit         = camera_usb_deinit,
    .start_capture  = camera_usb_start_capture,
    .stop_capture   = camera_usb_stop_capture,
    .get_frame      = camera_usb_get_frame,
    .set_param      = camera_usb_set_param,
    .get_capability = camera_usb_get_capability,
};

/* ============================================================================
 * Public Constructor & Destructor
 * ========================================================================== */
/**
 * @copydoc camera_usb_create
 */
camera_base_t *camera_usb_create(const char *dev_path,
                                 int width,
                                 int height,
                                 uint32_t fmt,
                                 uint32_t fps)
{
    if (!dev_path || width <= 0 || height <= 0)
        return NULL;

    // Allocate memory for subclass instance
    camera_usb_t *me = mem_calloc(1, sizeof(*me));
    if (!me) return NULL;

    // Initialize base class properties (OOP core)
    me->base.ops        = &g_camera_usb_ops;
    me->base.name       = "usb_camera";
    me->base.width      = width;
    me->base.height     = height;
    me->base.fps        = fps;
    me->base.is_init    = false;
    me->base.is_running = false;

    // Initialize private parameters
    me->dev_path    = dev_path;
    me->pixel_fmt   = fmt;
    me->fd          = -1;

    return &me->base;
}

/**
 * @copydoc camera_usb_destroy
 */
void camera_usb_destroy(camera_base_t *base_me)
{
    if (!base_me) return;
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);

    // Release hardware resources
    camera_deinit(base_me);
    // Free instance memory
    mem_free(me);

    printf("[USB Camera] Instance destroyed successfully\n");
}