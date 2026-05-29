/* SPDX-License-Identifier: MIT */
/**
 * @file    camera_base.c
 * @brief   Camera Abstract Base Class Implementation
 * @details V4L2 thin encapsulation and base class interface dispatch,
 *          pure hardware layer logic, no business code.
 * @author  LuoZhihong
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
 * Integrated industrial-grade self-test logic from legacy code.
 * ========================================================================== */

/**
 * @brief  Open device (blocking mode, optimal capture solution)
 */
int v4l2_open(const char *dev_path)
{
    int fd = open(dev_path, O_RDWR);
    return (fd < 0) ? -errno : fd;
}

/**
 * @brief  Close device
 */
void v4l2_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

/**
 * @brief  Stream on/off control
 */
int v4l2_stream_ctrl(int fd, bool on)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return ioctl(fd, on ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type) < 0 ? -errno : 0;
}

/* ====================== Core Self-Test Functions ====================== */
/**
 * @brief  Query device basic capability (from legacy _video_usb_open)
 */
int v4l2_query_capability(int fd, camera_capability_t *cap)
{
    struct v4l2_capability v4l2_cap;
    memset(&v4l2_cap, 0, sizeof(v4l2_cap));

    if (ioctl(fd, VIDIOC_QUERYCAP, &v4l2_cap) < 0)
        return -errno;

    /* Must support video capture and streaming */
    if (!(v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(v4l2_cap.capabilities & V4L2_CAP_STREAMING)) {
        return -ENODEV;
    }

    /* Fill capability info */
    strncpy(cap->device_name, (char *)v4l2_cap.card, sizeof(cap->device_name) - 1);
    strncpy(cap->bus_info, (char *)v4l2_cap.bus_info, sizeof(cap->bus_info) - 1);

    printf("[V4L2] Device self-test passed: %s, Bus: %s\n", cap->device_name, cap->bus_info);
    return 0;
}

/**
 * @brief  Check if a single control item is supported (from legacy _video_usb_check_control_support)
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
 * @brief  Enumerate all supported pixel formats (from legacy _video_usb_detect_capability)
 */
int v4l2_enum_formats(int fd, camera_capability_t *cap)
{
    struct v4l2_fmtdesc fmtdesc;
    uint32_t index = 0;

    memset(cap, 0, offsetof(camera_capability_t, support_exposure));

    while (1) {
        memset(&fmtdesc, 0, sizeof(fmtdesc));
        fmtdesc.index = index;
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0)
            break;

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
        index++;
    }

    /* Check AI control support */
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

/* ====================== Format & Parameter Operations ====================== */
/**
 * @brief  Set video format + read-back verification (from legacy _video_usb_set_format)
 */
int v4l2_set_format(int fd, int *width, int *height, uint32_t pixelformat)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* Configure target parameters */
    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    /* Try to set format */
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
        return -errno;

    /* Critical: Read-back verification, get actual effective parameters */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0)
        return -errno;

    /* Update to actual effective parameters */
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
 * @brief  Get current video format
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
 * @brief  Set FPS + read-back verification (from legacy _video_usb_set_fps)
 */
int v4l2_set_fps(int fd, uint32_t *fps)
{
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* Get current parameters */
    if (ioctl(fd, VIDIOC_G_PARM, &parm) < 0)
        return -errno;

    /* Set target FPS */
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = *fps;

    if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0)
        return -errno;

    /* Read back actual effective FPS */
    *fps = parm.parm.capture.timeperframe.denominator /
           parm.parm.capture.timeperframe.numerator;

    printf("[V4L2] FPS set success: %d fps\n", *fps);
    return 0;
}

/* ====================== Buffer Operations ====================== */
/**
 * @brief  Request buffers + return actual allocated count
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
 * @brief  Query buffer information
 */
int v4l2_querybuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_QUERYBUF, buf) < 0 ? -errno : 0;
}

/**
 * @brief  Enqueue buffer
 */
int v4l2_qbuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_QBUF, buf) < 0 ? -errno : 0;
}

/**
 * @brief  Dequeue buffer
 */
int v4l2_dqbuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_DQBUF, buf) < 0 ? -errno : 0;
}

/**
 * @brief  Memory map
 */
void *v4l2_mmap(int fd, size_t length, off_t offset)
{
    return mmap(NULL, length, PROT_READ, MAP_SHARED, fd, offset);
}

/**
 * @brief  Unmap memory
 */
void v4l2_munmap(void *addr, size_t length)
{
    if (addr != MAP_FAILED)
        munmap(addr, length);
}

/* ====================== Control Parameter Operations ====================== */
/**
 * @brief  Set control parameter
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
 * @brief  Get control parameter
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
 * Unified External Interface Implementation
 * V3.0 Mandatory Rule: NULL check + OPS validation + State management + Single dispatch
 * ========================================================================== */
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

int camera_deinit(camera_base_t *me)
{
    if (!me || !me->ops || !me->ops->deinit)
        return -EINVAL;

    int ret = me->ops->deinit(me);
    if (!ret)
        me->is_init = false;

    return ret;
}

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

int camera_stop_capture(camera_base_t *me)
{
    if (!me || !me->ops || !me->ops->stop_capture)
        return -EINVAL;

    int ret = me->ops->stop_capture(me);
    if (!ret)
        me->is_running = false;

    return ret;
}

int camera_get_frame(camera_base_t *me, void **frame, size_t *len)
{
    if (!me || !frame || !len || !me->ops || !me->ops->get_frame)
        return -EINVAL;

    return me->ops->get_frame(me, frame, len);
}

int camera_set_param(camera_base_t *me, int cmd, void *arg)
{
    if (!me || !arg || !me->ops)
        return -EINVAL;

    if (me->ops->set_param)
        return me->ops->set_param(me, cmd, arg);

    return -ENOSYS;
}

int camera_get_capability(camera_base_t *me, camera_capability_t *cap)
{
    if (!me || !cap || !me->ops || !me->ops->get_capability)
        return -EINVAL;

    return me->ops->get_capability(me, cap);
}