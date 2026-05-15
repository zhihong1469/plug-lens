#include "camera_base.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>

/* ============================================================================
 * V4L2 系统调用薄封装
 * V3.0 Device层规则：仅封装内核接口，无任何业务逻辑
 * 集成老代码工业级自检逻辑
 * ========================================================================== */

// 1. 打开设备（阻塞模式，最优采集方案）
int v4l2_open(const char *dev_path)
{
    int fd = open(dev_path, O_RDWR);
    return (fd < 0) ? -errno : fd;
}

// 2. 关闭设备
void v4l2_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

// 3. 流开关控制
int v4l2_stream_ctrl(int fd, bool on)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return ioctl(fd, on ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type) < 0 ? -errno : 0;
}

// ====================== 核心自检函数（移植自老代码）======================
// 3.1 查询设备基本能力（老代码 _video_usb_open 中的能力检测）
int v4l2_query_capability(int fd, camera_capability_t *cap)
{
    struct v4l2_capability v4l2_cap;
    memset(&v4l2_cap, 0, sizeof(v4l2_cap));

    if (ioctl(fd, VIDIOC_QUERYCAP, &v4l2_cap) < 0)
        return -errno;

    // 必须支持视频采集 + 流传输
    if (!(v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(v4l2_cap.capabilities & V4L2_CAP_STREAMING)) {
        return -ENODEV;
    }

    // 填充能力信息
    strncpy(cap->device_name, (char *)v4l2_cap.card, sizeof(cap->device_name) - 1);
    strncpy(cap->bus_info, (char *)v4l2_cap.bus_info, sizeof(cap->bus_info) - 1);

    printf("[V4L2] 设备自检通过: %s, 总线: %s\n", cap->device_name, cap->bus_info);
    return 0;
}

// 3.2 检测单个控制项是否支持（老代码 _video_usb_check_control_support）
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

// 3.3 枚举所有支持的像素格式（老代码 _video_usb_detect_capability）
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

    // 检测AI控制项支持
    cap->support_exposure = v4l2_check_control_support(fd, V4L2_CID_EXPOSURE_AUTO);
    cap->support_white_balance = v4l2_check_control_support(fd, V4L2_CID_AUTO_WHITE_BALANCE);
    cap->support_gain = v4l2_check_control_support(fd, V4L2_CID_AUTOGAIN);

    printf("[V4L2] 格式支持: YUYV=%s, MJPEG=%s, NV12=%s\n",
           cap->support_yuyv ? "YES" : "NO",
           cap->support_mjpeg ? "YES" : "NO",
           cap->support_nv12 ? "YES" : "NO");
    printf("[V4L2] 控制支持: 曝光=%s, 白平衡=%s, 增益=%s\n",
           cap->support_exposure ? "YES" : "NO",
           cap->support_white_balance ? "YES" : "NO",
           cap->support_gain ? "YES" : "NO");

    return 0;
}

// ====================== 格式与参数操作（带回读校验）======================
// 4.1 设置视频格式 + 回读校验（老代码 _video_usb_set_format 核心逻辑）
int v4l2_set_format(int fd, int *width, int *height, uint32_t pixelformat)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    // 配置目标参数
    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    // 尝试设置格式
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
        return -errno;

    // 【关键】回读校验，获取驱动实际生效的参数
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0)
        return -errno;

    // 更新为实际生效的参数
    *width = fmt.fmt.pix.width;
    *height = fmt.fmt.pix.height;

    printf("[V4L2] 格式设置成功: %dx%d, FourCC: %c%c%c%c\n",
           *width, *height,
           (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);

    return 0;
}

// 4.2 获取当前视频格式
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

// 4.3 设置帧率 + 回读校验（老代码 _video_usb_set_fps）
int v4l2_set_fps(int fd, uint32_t *fps)
{
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    // 获取当前参数
    if (ioctl(fd, VIDIOC_G_PARM, &parm) < 0)
        return -errno;

    // 设置目标帧率
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = *fps;

    if (ioctl(fd, VIDIOC_S_PARM, &parm) < 0)
        return -errno;

    // 回读实际生效的帧率
    *fps = parm.parm.capture.timeperframe.denominator /
           parm.parm.capture.timeperframe.numerator;

    printf("[V4L2] 帧率设置成功: %d fps\n", *fps);
    return 0;
}

// ====================== 缓冲区操作 ======================
// 5.1 申请缓冲区 + 返回实际分配数量
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
    printf("[V4L2] 申请缓冲区成功: 实际数量=%d\n", *buf_cnt);
    return 0;
}

// 5.2 查询缓冲区信息
int v4l2_querybuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_QUERYBUF, buf) < 0 ? -errno : 0;
}

// 5.3 入队缓冲区
int v4l2_qbuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_QBUF, buf) < 0 ? -errno : 0;
}

// 5.4 出队缓冲区
int v4l2_dqbuf(int fd, struct v4l2_buffer *buf)
{
    return ioctl(fd, VIDIOC_DQBUF, buf) < 0 ? -errno : 0;
}

// 5.5 内存映射
void *v4l2_mmap(int fd, size_t length, off_t offset)
{
    return mmap(NULL, length, PROT_READ, MAP_SHARED, fd, offset);
}

// 5.6 解除内存映射
void v4l2_munmap(void *addr, size_t length)
{
    if (addr != MAP_FAILED)
        munmap(addr, length);
}

// ====================== 控制参数操作（热配置）======================
// 6.1 设置控制参数
int v4l2_set_ctrl(int fd, uint32_t cid, int value)
{
    struct v4l2_control ctl;
    memset(&ctl, 0, sizeof(ctl));
    ctl.id = cid;
    ctl.value = value;

    return ioctl(fd, VIDIOC_S_CTRL, &ctl) < 0 ? -errno : 0;
}

// 6.2 获取控制参数
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

int camera_get_capability(camera_base_t *me, camera_capability_t *cap)
{
    if (!me || !cap || !me->ops || !me->ops->get_capability)
        return -EINVAL;

    return me->ops->get_capability(me, cap);
}