#include "../inc/camera_base.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>

/* ============================================================================
 * V4L2 系统调用薄封装  
 * V3.0 Device层规则：仅封装内核接口，无任何业务逻辑
 * ========================================================================== */
int v4l2_open(const char *dev_path)
{
    int fd = open(dev_path, O_RDWR); // 阻塞模式
    return (fd < 0) ? -errno : fd; 
}

void v4l2_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

int v4l2_set_format(int fd, int width, int height)
{
    struct v4l2_format fmt = {0};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = width;
    fmt.fmt.pix.height      = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    return ioctl(fd, VIDIOC_S_FMT, &fmt) < 0 ? -errno : 0;
}

int v4l2_reqbufs(int fd, int buf_cnt)
{
    struct v4l2_requestbuffers req = {0};
    req.count  = buf_cnt;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    return ioctl(fd, VIDIOC_REQBUFS, &req) < 0 ? -errno : 0;
}

int v4l2_querybuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_QUERYBUF, buf) < 0 ? -errno : 0;
}

int v4l2_qbuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_QBUF, buf) < 0 ? -errno : 0;
}

int v4l2_dqbuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_DQBUF, buf) < 0 ? -errno : 0;
}

void *v4l2_mmap(int fd, size_t length, off_t offset)
{
    return mmap(NULL, length, PROT_READ, MAP_SHARED, fd, offset);
}

void v4l2_munmap(void *addr, size_t length)
{
    if (addr != MAP_FAILED)
        munmap(addr, length);
}

int v4l2_stream_ctrl(int fd, bool on)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return ioctl(fd, on ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type) < 0 ? -errno : 0;
}

/* ============================================================================
 * 对外统一接口实现
 * V3.0 强制规则：空指针校验 + OPS合法性校验 + 状态管理 + 单一分发
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