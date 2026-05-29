/* SPDX-License-Identifier: MIT */
/**
 * @file    camera_base.c
 * @brief   Camera Abstract Base Class Implementation
 * @details Internal implementation:
 *          - Industrial-grade V4L2 kernel interface thin encapsulation
 *          - C-OOP polymorphic interface dispatch
 *          - State machine management for camera lifecycle
 *          - Closed-loop buffer management (auto recycle)
 *          - Pure hardware layer logic, no business code
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "camera_base.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>

/* ============================================================================
 * V4L2 System Call Thin Encapsulation
 * V3.0 Device Layer Rule: Only encapsulate kernel interfaces, NO business logic.
 * Industrial-grade implementation with read-back verification and error handling.
 * ========================================================================== */

/**
 * @brief   Open V4L2 device in blocking read-write mode
 * @details Optimal mode for embedded video capture
 */
int v4l2_open(const char *dev_path)
{
    int fd = open(dev_path, O_RDWR);
    return (fd < 0) ? -errno : fd;
}

/**
 * @brief   Safely close V4L2 device file descriptor
 */
void v4l2_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

/**
 * @brief   Control V4L2 video stream state (ON/OFF)
 */
int v4l2_stream_ctrl(int fd, bool on)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return ioctl(fd, on ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type) < 0 ? -errno : 0;
}

/* ====================== Core Device Self-Test Functions ====================== */
/**
 * @brief   Query and validate core V4L2 device capabilities
 * @details Verify capture and streaming support, fill device info
 */
int v4l2_query_capability(int fd, camera_capability_t *cap)
{
    struct v4l2_capability v4l2_cap;
    memset(&v4l2_cap, 0, sizeof(v4l2_cap));

    if (ioctl(fd, VIDIOC_QUERYCAP, &v4l2_cap) < 0)
        return -errno;

    /* Mandatory: Device must support video capture and streaming */
    if (!(v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(v4l2_cap.capabilities & V4L2_CAP_STREAMING)) {
        return -ENODEV;
    }

    /* Copy device information safely */
    strncpy(cap->device_name, (char *)v4l2_cap.card, sizeof(cap->device_name) - 1);
    strncpy(cap->bus_info, (char *)v4l2_cap.bus_info, sizeof(cap->bus_info) - 1);

    printf("[V4L2] Device self-test passed: %s, Bus: %s\n", cap->device_name, cap->bus_info);
    return 0;
}

/**
 * @brief   Check if a V4L2 control is supported and enabled
 */
int v4l2_check_control_support(int fd, uint32_t cid)
{
    struct v4l2_queryctrl qctrl;
    memset(&qctrl, 0, sizeof(qctrl));
    qctrl.id = cid;

    if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) < 0)
        return 0;

    if (qctrl.flags & V4L2_CTRL_FLAG_DISABLED)
        return 0;

    return 1;
}

/**
 * @brief   Enumerate all pixel formats and control capabilities
 * @details Auto-detect supported formats and hardware controls
 */
int v4l2_enum_formats(int fd, camera_capability_t *cap)
{
    struct v4l2_fmtdesc fmtdesc;
    uint32_t index = 0;

    memset(cap, 0, offsetof(camera_capability_t, support_exposure));

    while (1) {
        memset(&fmtdesc, 0, sizeof(fmtdesc));
        fmtdesc.index = index++;
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0)
            break;

        /* Mark supported pixel formats */
        switch (fmtdesc.pixelformat) {
            case V4L2_PIX_FMT_YUYV:
                cap->support_yuyv = true;
                break;
            case V4L2_PIX_FMT_MJPEG:
                cap->support_mjpeg = true;
                break;
            case V4L2_PIX_FMT_NV12:
                cap->support_nv12 = true;
                break;
        }
    }

    /* Detect AI-related control support */
    cap->support_exposure = v4l2_check_control_support(fd, V4L2_CID_EXPOSURE_AUTO);
    cap->support_white_balance = v4l2_check_control_support(fd, V4L2_CID_AUTO_WHITE_BALANCE);
    cap->support_gain = v4l2_check_control_support(fd, V4L2_CID_AUTOGAIN);

    printf("[V4L2] Format support: YUYV=%s, MJPEG=%s, NV12=%s\n",
           cap->support_yuyv ? "YES" : "NO",
           cap->support_mjpeg ? "YES" : "NO",
           cap->support_nv12 ? "YES" : "NO");
    printf("[V4L2] Control support: Exposure=%s, WB=%s, Gain=%s\n",
           cap->support_exposure ? "YES" : "NO",
           cap->support_white_balance ? "YES" : "NO",
           cap->support_gain ? "YES" : "NO");

    return 0;
}

/* ====================== Format & Parameter Configuration ====================== */
/**
 * @brief   Set video format with hardware read-back verification
 * @details Critical for embedded systems: Confirm actual hardware-supported resolution
 */
int v4l2_set_format(int fd, int *width, int *height, uint32_t pixelformat)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* Set target format parameters */
    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
        return -errno;

    /* Read back actual hardware configuration */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0)
        return -errno;

    /* Update to real effective values */
    *width = fmt.fmt.pix.width;
    *height = fmt.fmt.pix.height;

    printf("[V4L2] Format set success: %dx%d, FourCC: %c%c%c%c\n",
           *width, *height,
           (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);

    return 0;
}

/**
 * @brief   Get current active video format from hardware
 */
int v4l2_get_format(int fd, int *width, int *height, uint32_t *pixelformat)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0)
        return -errno;

    *width = fmt.fmt.pix.width;
    *height = fmt.fmt.pix.height;
    *pixelformat = fmt.fmt.pix.pixelformat;

    return 0;
}

/**
 * @brief   Set frame rate with hardware read-back verification
 */
int v4l2_set_fps(int fd, uint32_t *fps)
{
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(fd, VIDIOC_G_PARM, &parm) < 0)
        return -errno;

    /* Configure target frame rate */
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = *fps;

    if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0)
        return -errno;

    /* Get actual supported FPS */
    *fps = parm.parm.capture.timeperframe.denominator /
           parm.parm.capture.timeperframe.numerator;

    printf("[V4L2] FPS set success: %d fps\n", *fps);
    return 0;
}

/* ====================== Buffer Management Operations ====================== */
/**
 * @brief   Request MMAP buffers from V4L2 kernel
 */
int v4l2_reqbufs(int fd, int *buf_cnt)
{
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = *buf_cnt;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0)
        return -errno;

    *buf_cnt = req.count;
    printf("[V4L2] Request buffers success: Actual count=%d\n", *buf_cnt);
    return 0;
}

/**
 * @brief   Query kernel buffer metadata
 */
int v4l2_querybuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_QUERYBUF, buf) < 0 ? -errno : 0;
}

/**
 * @brief   Enqueue buffer to kernel driver
 */
int v4l2_qbuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_QBUF, buf) < 0 ? -errno : 0;
}

/**
 * @brief   Dequeue filled buffer from kernel driver
 */
int v4l2_dqbuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_DQBUF, buf) < 0 ? -errno : 0;
}

/**
 * @brief   Map kernel buffer to user space
 */
void *v4l2_mmap(int fd, size_t length, off_t offset)
{
    return mmap(NULL, length, PROT_READ, MAP_SHARED, fd, offset);
}

/**
 * @brief   Unmap user-space buffer safely
 */
void v4l2_munmap(void *addr, size_t length)
{
    if (addr != MAP_FAILED)
        munmap(addr, length);
}

/* ====================== Control Parameter Operations ====================== */
/**
 * @brief   Set V4L2 device control register
 */
int v4l2_set_ctrl(int fd, uint32_t cid, int value)
{
    struct v4l2_control ctl;
    memset(&ctl, 0, sizeof(ctl));
    ctl.id = cid;
    ctl.value = value;

    return ioctl(fd, VIDIOC_S_CTRL, &ctl) < 0 ? -errno : 0;
}

/**
 * @brief   Get V4L2 device control register value
 */
int v4l2_get_ctrl(int fd, uint32_t cid, int *value)
{
    struct v4l2_control ctl;
    memset(&ctl, 0, sizeof(ctl));
    ctl.id = cid;

    if (ioctl(fd, VIDIOC_G_CTRL, &ctl) < 0)
        return -errno;

    *value = ctl.value;
    return 0;
}

/* ============================================================================
 * Unified Public API Implementation
 * V3.0 Mandatory Rule: NULL check + OPS validation + State management + Single dispatch
 * ============================================================================ */

/**
 * @brief   Public API: Initialize camera instance
 * @details Validate pointer/ops, prevent re-initialization, update state machine
 */
int camera_init(camera_base_t *me)
{
    if (!me || !me->ops || !me->ops->init)
        return -EINVAL;

    if (me->is_init)
        return 0;

    int ret = me->ops->init(me);
    if (!ret)
        me->is_init = true;

    return ret;
}

/**
 * @brief   Public API: De-initialize camera instance
 * @details Call subclass deinit, reset state to uninitialized
 */
int camera_deinit(camera_base_t *me)
{
    if (!me || !me->ops || !me->ops->deinit)
        return -EINVAL;

    int ret = me->ops->deinit(me);
    if (!ret)
        me->is_init = false;

    return ret;
}

/**
 * @brief   Public API: Start video capture
 * @details Validate state, prevent duplicate start, update running flag
 */
int camera_start_capture(camera_base_t *me)
{
    if (!me || !me->ops || !me->ops->start_capture)
        return -EINVAL;

    if (me->is_running)
        return 0;

    int ret = me->ops->start_capture(me);
    if (!ret)
        me->is_running = true;

    return ret;
}

/**
 * @brief   Public API: Stop video capture
 * @details Call subclass stop, update running state
 */
int camera_stop_capture(camera_base_t *me)
{
    if (!me || !me->ops || !me->ops->stop_capture)
        return -EINVAL;

    int ret = me->ops->stop_capture(me);
    if (!ret)
        me->is_running = false;

    return ret;
}

/**
 * @brief   Public API: Get video frame data
 * @details Strict parameter validation, dispatch to subclass implementation
 */
int camera_get_frame(camera_base_t *me, void **frame, size_t *len)
{
    if (!me || !frame || !len || !me->ops || !me->ops->get_frame)
        return -EINVAL;

    return me->ops->get_frame(me, frame, len);
}

/**
 * @brief   Public API: Set camera parameters
 * @details Validate input, support unimplemented interfaces
 */
int camera_set_param(camera_base_t *me, int cmd, void *arg)
{
    if (!me || !arg || !me->ops)
        return -EINVAL;

    if (me->ops->set_param)
        return me->ops->set_param(me, cmd, arg);

    return -ENOSYS;
}

/**
 * @brief   Public API: Get device capabilities
 * @details Validate pointers, dispatch to subclass implementation
 */
int camera_get_capability(camera_base_t *me, camera_capability_t *cap)
{
    if (!me || !cap || !me->ops || !me->ops->get_capability)
        return -EINVAL;

    return me->ops->get_capability(me, cap);
}