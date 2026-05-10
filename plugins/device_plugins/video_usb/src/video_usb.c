#include "video_usb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/types.h>
#include "log.h"

// ==========================================================================
// 内部辅助函数：格式转换
// ==========================================================================
static uint32_t _video_usb_format_to_fourcc(video_format_t fmt)
{
    if (fmt == VIDEO_PIX_FMT_YUYV) return V4L2_PIX_FMT_YUYV;
    if (fmt == VIDEO_PIX_FMT_NV12) return V4L2_PIX_FMT_NV12;
    if (fmt == VIDEO_PIX_FMT_MJPEG) return V4L2_PIX_FMT_MJPEG;
    return V4L2_PIX_FMT_YUYV;
}

static video_format_t _video_usb_fourcc_to_format(uint32_t fourcc)
{
    if (fourcc == V4L2_PIX_FMT_YUYV) return VIDEO_PIX_FMT_YUYV;
    if (fourcc == V4L2_PIX_FMT_NV12) return VIDEO_PIX_FMT_NV12;
    if (fourcc == V4L2_PIX_FMT_MJPEG) return VIDEO_PIX_FMT_MJPEG;
    return VIDEO_PIX_FMT_YUYV;
}

static bool _video_usb_check_control_support(int fd, uint32_t cid)
{
    struct v4l2_queryctrl qctrl;
    memset(&qctrl, 0, sizeof(qctrl));
    qctrl.id = cid;
    if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) < 0) {
        return false;
    }
    if (qctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
        return false;
    }
    return true;
}

// ==========================================================================
// 对外实现：纯硬件操作（所有函数接收ctx参数）
// ==========================================================================

int _video_usb_open(const char *dev_path, video_usb_context_t **out_ctx)
{
    // 分配上下文
    video_usb_context_t *ctx = (video_usb_context_t*)malloc(sizeof(video_usb_context_t));
    if (ctx == NULL) {
        LOG_E("Video USB: malloc context failed");
        return -1;
    }
    memset(ctx, 0, sizeof(video_usb_context_t));

    // 打开设备
    ctx->fd = open(dev_path, O_RDWR);
    if (ctx->fd < 0) {
        LOG_E("Video USB: open %s failed, errno=%d (%s)", dev_path, errno, strerror(errno));
        free(ctx);
        return -1;
    }
    LOG_I("Video USB: open %s success, fd=%d", dev_path, ctx->fd);

    // 查询能力
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        LOG_E("Video USB: VIDIOC_QUERYCAP failed");
        close(ctx->fd);
        free(ctx);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        LOG_E("Video USB: device not support capture/streaming");
        close(ctx->fd);
        free(ctx);
        return -1;
    }

    strncpy(ctx->cap.device_name, (char*)cap.card, sizeof(ctx->cap.device_name) - 1);
    strncpy(ctx->cap.bus_info, (char*)cap.bus_info, sizeof(ctx->cap.bus_info) - 1);
    LOG_I("Video USB: device info: %s, bus: %s", ctx->cap.device_name, ctx->cap.bus_info);

    *out_ctx = ctx;
    return 0;
}

void _video_usb_close(video_usb_context_t *ctx)
{
    if (ctx == NULL) return;
    LOG_I("Video USB: closing device, fd=%d", ctx->fd);

    // 停止流
    if (ctx->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
        LOG_I("Video USB: streamoff success");
    }

    // 释放MMAP
    for (uint32_t i = 0; i < ctx->buf_count; i++) {
        if (ctx->buffers[i] != MAP_FAILED) {
            munmap(ctx->buffers[i], ctx->buffer_lengths[i]);
            LOG_D("Video USB: munmap buffer[%d] success", i);
        }
    }

    // 关闭设备
    if (ctx->fd >= 0) {
        close(ctx->fd);
    }

    // 释放上下文
    free(ctx);
    LOG_I("Video USB: closed and free context");
}

int _video_usb_detect_capability(video_usb_context_t *ctx)
{
    struct v4l2_fmtdesc fmtdesc;
    uint32_t index = 0;

    // 检测格式支持
    while (1) {
        memset(&fmtdesc, 0, sizeof(fmtdesc));
        fmtdesc.index = index;
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(ctx->fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
            break;
        }

        if (fmtdesc.pixelformat == V4L2_PIX_FMT_YUYV) {
            ctx->cap.support_yuyv = true;
        } else if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
            ctx->cap.support_mjpeg = true;
        } else if (fmtdesc.pixelformat == V4L2_PIX_FMT_NV12) {
            ctx->cap.support_nv12 = true;
        }

        index++;
    }

    // 检测AI控制支持
    ctx->cap.support_manual_exposure = _video_usb_check_control_support(ctx->fd, V4L2_CID_EXPOSURE_AUTO);
    ctx->cap.support_lock_white_balance = _video_usb_check_control_support(ctx->fd, V4L2_CID_AUTO_WHITE_BALANCE);
    ctx->cap.support_lock_gain = _video_usb_check_control_support(ctx->fd, V4L2_CID_AUTOGAIN);

    return 0;
}

int _video_usb_set_format(video_usb_context_t *ctx)
{
    struct v4l2_format fmt;
    int ret;

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = ctx->config.width;
    fmt.fmt.pix.height = ctx->config.height;
    fmt.fmt.pix.pixelformat = _video_usb_format_to_fourcc(ctx->config.format);
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    // 【1】尝试设置格式
    LOG_I("Video USB: Trying to set format: %dx%d, FourCC: %c%c%c%c",
          ctx->config.width, ctx->config.height,
          (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 24) & 0xFF);

    ret = ioctl(ctx->fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        LOG_E("Video USB: VIDIOC_S_FMT failed, errno=%d (%s)", errno, strerror(errno));
        return -1;
    }

    // 【2】【关键】立刻读回来，看看驱动到底改成了什么！
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(ctx->fd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        LOG_E("Video USB: VIDIOC_G_FMT failed (verify)");
        return -1;
    }

    // 【3】打印驱动实际返回的参数
    LOG_I("Video USB: Driver ACTUALLY set: %dx%d, FourCC: %c%c%c%c, SizeImage: %u",
          fmt.fmt.pix.width, fmt.fmt.pix.height,
          (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 24) & 0xFF,
          fmt.fmt.pix.sizeimage);

    // 更新我们的配置，以驱动返回的为准
    ctx->config.width = fmt.fmt.pix.width;
    ctx->config.height = fmt.fmt.pix.height;
    ctx->config.format = _video_usb_fourcc_to_format(fmt.fmt.pix.pixelformat);

    return 0;
}

int _video_usb_set_fps(video_usb_context_t *ctx, uint32_t fps)
{
    struct v4l2_streamparm parm;
    int ret;

    if (fps == 0){
        LOG_E("Video USB: set fps failed, fps=0");
        return -1;
    }
    LOG_I("Video USB: try set fps = %u", fps);

    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(ctx->fd, VIDIOC_G_PARM, &parm);
    (void)ret;

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;

    ret = ioctl(ctx->fd, VIDIOC_S_PARM, &parm);
    if (ret < 0) {
        LOG_E("Video USB: set fps %u failed, errno=%d", fps, errno);
        return -1;
    }
    LOG_I("Video USB: set fps %u success", fps);

    return 0;
}

int _video_usb_alloc_buffers(video_usb_context_t *ctx)
{
    struct v4l2_requestbuffers req;
    int ret;

    memset(&req, 0, sizeof(req));
    req.count = ctx->config.buf_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    LOG_I("Video USB: request buffers count = %u", ctx->config.buf_count);

    ret = ioctl(ctx->fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        LOG_E("Video USB: VIDIOC_REQBUFS failed, errno=%d", errno);
        return -1;
    }

    ctx->buf_count = req.count;
    LOG_I("Video USB: alloc buffers success, actual count = %u", ctx->buf_count);
    return 0;
}

int _video_usb_mmap_buffers(video_usb_context_t *ctx)
{
    struct v4l2_buffer buf;
    int ret;

    for (uint32_t i = 0; i < ctx->buf_count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(ctx->fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) {
            LOG_E("Video USB: query buffer[%d] failed", i);
            return -1;
        }

        ctx->buffer_lengths[i] = buf.length;
        ctx->buffers[i] = mmap(NULL, buf.length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED,
                                   ctx->fd, buf.m.offset);

        if (ctx->buffers[i] == MAP_FAILED) {
            LOG_E("Video USB: mmap buffer[%d] failed, len=%u", i, buf.length);
            return -1;
        }
        LOG_D("Video USB: mmap buffer[%d] success, addr=%p, len=%u", i, ctx->buffers[i], buf.length);
    }
    LOG_I("Video USB: mmap all %u buffers success", ctx->buf_count);
    return 0;
}

int _video_usb_queue_all_buffers(video_usb_context_t *ctx)
{
    struct v4l2_buffer buf;
    int ret;

    LOG_I("Video USB: queue all buffers...");
    for (uint32_t i = 0; i < ctx->buf_count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(ctx->fd, VIDIOC_QBUF, &buf);
        if (ret < 0) {
            LOG_E("Video USB: queue buffer[%d] failed", i);
            return -1;
        }
        LOG_D("Video USB: queue buffer[%d] success", i);
    }
    LOG_I("Video USB: queue all %u buffers success", ctx->buf_count);
    return 0;
}

int _video_usb_streamon(video_usb_context_t *ctx)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
        LOG_E("Video USB: streamon failed, errno=%d", errno);
        return -1;
    }
    LOG_I("Video USB: streamon success, start capture");
    return 0;
}

int _video_usb_streamoff(video_usb_context_t *ctx)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) < 0) {
        LOG_E("Video USB: streamoff failed, errno=%d", errno);
        return -1;
    }
    LOG_I("Video USB: streamoff success, stop capture");
    return 0;
}

int _video_usb_dqbuf(video_usb_context_t *ctx, video_frame_t *frame)
{
    int ret;
    struct pollfd fds;
    struct v4l2_buffer buf;

    memset(&fds, 0, sizeof(fds));
    fds.fd = ctx->fd;
    fds.events = POLLIN;
    ret = poll(&fds, 1, 1000); // 1s超时

    // 【关键】打印poll状态
    if (ret == -1) {
        LOG_E("Video USB: poll error, errno=%d (%s)", errno, strerror(errno));
        return -1;
    }
    if (ret == 0) {
        LOG_W("Video USB: poll timeout(1s), no frame data");
        return -1;
    }

    // 出队缓冲区
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    ret = ioctl(ctx->fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        LOG_E("Video USB: dqbuf failed, errno=%d", errno);
        return -1;
    }

    if (buf.index >= ctx->buf_count) {
        LOG_E("Video USB: invalid buffer index %u", buf.index);
        ioctl(ctx->fd, VIDIOC_QBUF, &buf);
        return -1;
    }

    // 【核心】打印每帧采集信息（排查帧匹配问题）
    frame->data = ctx->buffers[buf.index];
    frame->length = buf.bytesused;
    frame->timestamp = (uint64_t)buf.timestamp.tv_sec * 1000000 + buf.timestamp.tv_usec;
    frame->index = buf.index;
    LOG_D("Video USB: dqbuf success, index=%u, bytes=%u, ts=%llu",
          buf.index, buf.bytesused, frame->timestamp);

    return 0;
}

int _video_usb_qbuf(video_usb_context_t *ctx, uint32_t index)
{
    struct v4l2_buffer buf;
    int ret;

    if (index >= ctx->buf_count) {
        LOG_E("Video USB: qbuf invalid index %u", index);
        return -1;
    }

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;

    ret = ioctl(ctx->fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
        // LOG_E("Video USB: qbuf index=%u failed, errno=%d", index, errno);
        return -1;
    }
    LOG_D("Video USB: qbuf success, index=%u", index);
    return 0;
}

int _video_usb_lock_ai_params(video_usb_context_t *ctx)
{
    struct v4l2_control ctl;
    int ret;
    LOG_I("Video USB: lock AI params(exposure/wb/gain)");

    if (ctx->config.lock_exposure && ctx->cap.support_manual_exposure) {
        memset(&ctl, 0, sizeof(ctl));
        ctl.id = V4L2_CID_EXPOSURE_AUTO;
        ctl.value = V4L2_EXPOSURE_MANUAL;
        ret = ioctl(ctx->fd, VIDIOC_S_CTRL, &ctl);
        LOG_I("Video USB: lock exposure %s", ret == 0 ? "success" : "failed");
        (void)ret;
    }

    if (ctx->config.lock_white_balance && ctx->cap.support_lock_white_balance) {
        memset(&ctl, 0, sizeof(ctl));
        ctl.id = V4L2_CID_AUTO_WHITE_BALANCE;
        ctl.value = 0;
        ret = ioctl(ctx->fd, VIDIOC_S_CTRL, &ctl);
        LOG_I("Video USB: lock white balance %s", ret == 0 ? "success" : "failed");
        (void)ret;
    }

    if (ctx->config.lock_gain && ctx->cap.support_lock_gain) {
        memset(&ctl, 0, sizeof(ctl));
        ctl.id = V4L2_CID_AUTOGAIN;
        ctl.value = 0;
        ret = ioctl(ctx->fd, VIDIOC_S_CTRL, &ctl);
        LOG_I("Video USB: lock gain %s", ret == 0 ? "success" : "failed");
        (void)ret;
    }

    return 0;
}

int _video_usb_enum_sizes(video_usb_context_t *ctx, video_format_t fmt, uint32_t (*sizes)[2], uint32_t max_cnt)
{
    struct v4l2_frmsizeenum fsize;
    uint32_t count = 0;
    uint32_t fourcc = _video_usb_format_to_fourcc(fmt);

    memset(&fsize, 0, sizeof(fsize));
    fsize.index = 0;
    fsize.pixel_format = fourcc;

    while (ioctl(ctx->fd, VIDIOC_ENUM_FRAMESIZES, &fsize) == 0) {
        if (count >= max_cnt) break;

        if (fsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            sizes[count][0] = fsize.discrete.width;
            sizes[count][1] = fsize.discrete.height;
            count++;
        } else if (fsize.type == V4L2_FRMSIZE_TYPE_STEPWISE || 
                   fsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
            if (count == 0) {
                sizes[count][0] = fsize.stepwise.max_width;
                sizes[count][1] = fsize.stepwise.max_height;
                count++;
            }
            break;
        }

        fsize.index++;
    }
    LOG_I("Video USB: enum %u sizes for format", count);
    return count;
}

int _video_usb_enum_fps(video_usb_context_t *ctx, video_format_t fmt, uint32_t w, uint32_t h, uint32_t *fps, uint32_t max_cnt)
{
    struct v4l2_frmivalenum fival;
    uint32_t count = 0;
    uint32_t fourcc = _video_usb_format_to_fourcc(fmt);

    memset(&fival, 0, sizeof(fival));
    fival.index = 0;
    fival.pixel_format = fourcc;
    fival.width = w;
    fival.height = h;

    while (ioctl(ctx->fd, VIDIOC_ENUM_FRAMEINTERVALS, &fival) == 0) {
        if (count >= max_cnt) break;

        if (fival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            if (fival.discrete.numerator > 0) {
                fps[count] = fival.discrete.denominator / fival.discrete.numerator;
                count++;
            }
        } else {
            break;
        }
        fival.index++;
    }
    LOG_I("Video USB: enum %u fps for %ux%d", count, w, h);
    return count;
}

int _video_usb_dump_yuv(const video_frame_t *frame, const char *filepath)
{
    if (frame == NULL || filepath == NULL || frame->data == NULL) {
        return -1;
    }

    FILE *fp = fopen(filepath, "wb");
    if (fp == NULL) {
        LOG_E("Video USB: dump yuv open %s failed", filepath);
        return -1;
    }

    size_t written = fwrite(frame->data, 1, frame->length, fp);
    if (written != frame->length) {
        LOG_E("Video USB: dump yuv write failed");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    LOG_I("Video USB: dump yuv success: %s, size=%zu", filepath, written);
    return 0;
}

// 新增：获取USB摄像头设备fd
int _video_usb_get_fd(video_usb_context_t *ctx)
{
    if (ctx == NULL) return -1;
    return ctx->fd;
}