#include "v4l2_video.h"
#include "log.h"

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
#include <linux/videodev2.h>
#include <pthread.h>

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define V4L2_VIDEO_MAX_BUFS 32

// ==========================================================================
// 内部静态变量
// ==========================================================================
static int g_fd = -1;
static bool g_is_init = false;
static bool g_is_streaming = false;
static v4l2_video_config_t g_config;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

// 【新增】摄像头能力检测结果
static v4l2_video_capability_t g_capability;

typedef struct {
    void *start;
    uint32_t length;
} v4l2_video_buffer_t;

static v4l2_video_buffer_t g_buffers[V4L2_VIDEO_MAX_BUFS];
static uint32_t g_buf_count = 0;

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static v4l2_video_err_t _v4l2_enum_formats(void);
static v4l2_video_err_t _v4l2_set_format(void);
static v4l2_video_err_t _v4l2_alloc_buffers(void);
static v4l2_video_err_t _v4l2_mmap_buffers(void);
static v4l2_video_err_t _v4l2_queue_all_buffers(void);
static v4l2_video_err_t _v4l2_lock_ai_params(void);
static uint32_t _v4l2_format_to_fourcc(v4l2_video_format_t fmt);
static v4l2_video_format_t _fourcc_to_v4l2_format(uint32_t fourcc);

// 【新增】能力检测内部函数
static v4l2_video_err_t _v4l2_detect_capability(void);
static bool _v4l2_check_control_support(uint32_t cid);

// ==========================================================================
// 错误码字符串表
// ==========================================================================
static const char* g_err_str[] = {
    [V4L2_VIDEO_OK] = "Success",
    [V4L2_VIDEO_ERR_OPEN] = "Failed to open device",
    [V4L2_VIDEO_ERR_QUERYCAP] = "Failed to query device capabilities",
    [V4L2_VIDEO_ERR_NOT_CAPTURE] = "Device does not support video capture",
    [V4L2_VIDEO_ERR_NOT_STREAMING] = "Device does not support streaming I/O",
    [V4L2_VIDEO_ERR_ENUM_FMT] = "Failed to enumerate formats",
    [V4L2_VIDEO_ERR_SET_FMT] = "Failed to set format",
    [V4L2_VIDEO_ERR_REQBUFS] = "Failed to request buffers",
    [V4L2_VIDEO_ERR_QUERYBUF] = "Failed to query buffer",
    [V4L2_VIDEO_ERR_MMAP] = "Failed to mmap buffer",
    [V4L2_VIDEO_ERR_QBUF] = "Failed to queue buffer",
    [V4L2_VIDEO_ERR_STREAMON] = "Failed to start stream",
    [V4L2_VIDEO_ERR_STREAMOFF] = "Failed to stop stream",
    [V4L2_VIDEO_ERR_POLL] = "Poll failed or timeout",
    [V4L2_VIDEO_ERR_DQBUF] = "Failed to dequeue buffer",
    [V4L2_VIDEO_ERR_LOCK] = "Failed to lock mutex",
    [V4L2_VIDEO_ERR_UNLOCK] = "Failed to unlock mutex",
    [V4L2_VIDEO_ERR_INVALID_PARAM] = "Invalid parameter",
    [V4L2_VIDEO_ERR_NOT_INIT] = "Module not initialized",
    [V4L2_VIDEO_ERR_ALREADY_INIT] = "Module already initialized",
    [V4L2_VIDEO_ERR_MUNMAP] = "Failed to munmap buffer",
    [V4L2_VIDEO_ERR_CLOSE] = "Failed to close device"
};

// ==========================================================================
// 【新增】对外API：获取摄像头能力检测结果
// ==========================================================================
const v4l2_video_capability_t* v4l2_video_get_capability(void)
{
    return &g_capability;
}

// ==========================================================================
// 对外API实现
// ==========================================================================

v4l2_video_err_t v4l2_video_init(const v4l2_video_config_t *config)
{
    v4l2_video_err_t err;
    int ret;

    if (config == NULL || config->dev_path == NULL) {
        return V4L2_VIDEO_ERR_INVALID_PARAM;
    }

    if (pthread_mutex_lock(&g_mutex) != 0) {
        return V4L2_VIDEO_ERR_LOCK;
    }

    if (g_is_init) {
        pthread_mutex_unlock(&g_mutex);
        return V4L2_VIDEO_ERR_ALREADY_INIT;
    }

    // 清空能力检测结果
    memset(&g_capability, 0, sizeof(g_capability));

    memcpy(&g_config, config, sizeof(v4l2_video_config_t));
    if (g_config.buf_count == 0) g_config.buf_count = 4;
    if (g_config.buf_count > V4L2_VIDEO_MAX_BUFS) g_config.buf_count = V4L2_VIDEO_MAX_BUFS;

    LOG_I("Initializing V4L2 video: dev=%s, %ux%u, fmt=%d, fps=%u, bufs=%u",
          g_config.dev_path, g_config.width, g_config.height,
          g_config.format, g_config.fps, g_config.buf_count);

    g_fd = open(g_config.dev_path, O_RDWR);
    if (g_fd < 0) {
        LOG_E("Open %s failed: %s", g_config.dev_path, strerror(errno));
        err = V4L2_VIDEO_ERR_OPEN;
        goto error_unlock;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    ret = ioctl(g_fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        LOG_E("VIDIOC_QUERYCAP failed: %s", strerror(errno));
        err = V4L2_VIDEO_ERR_QUERYCAP;
        goto error_close;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOG_E("Device does not support video capture");
        err = V4L2_VIDEO_ERR_NOT_CAPTURE;
        goto error_close;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        LOG_E("Device does not support streaming I/O");
        err = V4L2_VIDEO_ERR_NOT_STREAMING;
        goto error_close;
    }

    // 保存基础信息
    strncpy(g_capability.device_name, (char*)cap.card, sizeof(g_capability.device_name) - 1);
    strncpy(g_capability.bus_info, (char*)cap.bus_info, sizeof(g_capability.bus_info) - 1);
    LOG_I("Device: %s, Bus: %s", g_capability.device_name, g_capability.bus_info);

    // 【核心】一键检测摄像头所有核心能力
    err = _v4l2_detect_capability();
    if (err != V4L2_VIDEO_OK) {
        // 检测失败不影响初始化，继续执行
        LOG_W("Capability detect failed (non-critical)");
    }

    // 枚举格式（仅用于调试）
    (void)_v4l2_enum_formats();

    err = _v4l2_set_format();
    if (err != V4L2_VIDEO_OK) {
        goto error_close;
    }

    err = _v4l2_alloc_buffers();
    if (err != V4L2_VIDEO_OK) {
        goto error_close;
    }

    err = _v4l2_mmap_buffers();
    if (err != V4L2_VIDEO_OK) {
        goto error_free_bufs;
    }

    // 锁定AI参数（根据检测结果自动适配）
    if (g_config.lock_exposure || g_config.lock_white_balance || g_config.lock_gain) {
        (void)_v4l2_lock_ai_params();
    }

    g_is_init = true;
    LOG_I("V4L2 video init success");
    pthread_mutex_unlock(&g_mutex);
    return V4L2_VIDEO_OK;

error_free_bufs:
    for (uint32_t i = 0; i < g_buf_count; i++) {
        if (g_buffers[i].start != MAP_FAILED) {
            munmap(g_buffers[i].start, g_buffers[i].length);
        }
    }
    g_buf_count = 0;
error_close:
    close(g_fd);
    g_fd = -1;
error_unlock:
    pthread_mutex_unlock(&g_mutex);
    return err;
}

v4l2_video_err_t v4l2_video_start(void)
{
    v4l2_video_err_t err;
    int ret;

    if (pthread_mutex_lock(&g_mutex) != 0) return V4L2_VIDEO_ERR_LOCK;
    if (!g_is_init) { err = V4L2_VIDEO_ERR_NOT_INIT; goto error; }
    if (g_is_streaming) { err = V4L2_VIDEO_OK; goto error; }

    err = _v4l2_queue_all_buffers();
    if (err != V4L2_VIDEO_OK) goto error;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(g_fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOG_E("VIDIOC_STREAMON failed: %s", strerror(errno));
        err = V4L2_VIDEO_ERR_STREAMON;
        goto error;
    }

    g_is_streaming = true;
    LOG_I("V4L2 stream started");
    err = V4L2_VIDEO_OK;
error:
    pthread_mutex_unlock(&g_mutex);
    return err;
}

v4l2_video_err_t v4l2_video_get_frame(v4l2_video_frame_t *frame)
{
    int ret;
    struct pollfd fds;
    struct v4l2_buffer buf;

    if (frame == NULL) return V4L2_VIDEO_ERR_INVALID_PARAM;
    if (pthread_mutex_lock(&g_mutex) != 0) return V4L2_VIDEO_ERR_LOCK;
    if (!g_is_init || !g_is_streaming) {
        pthread_mutex_unlock(&g_mutex);
        return V4L2_VIDEO_ERR_NOT_INIT;
    }

    memset(&fds, 0, sizeof(fds));
    fds.fd = g_fd;
    fds.events = POLLIN;
    ret = poll(&fds, 1, 3000);

    // 被信号打断(Ctrl+C 主动退出) → 直接返回，**不打印任何错误**
    if (ret == -1 && errno == EINTR) {
        pthread_mutex_unlock(&g_mutex);
        return V4L2_VIDEO_ERR_POLL;
    }

    // 只有【真·超时/硬件错误】才打印日志
    if (ret <= 0) {
        LOG_E("Poll timeout or error: %s", strerror(errno));
        pthread_mutex_unlock(&g_mutex);
        return V4L2_VIDEO_ERR_POLL;
    }
    // ==================================================

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    ret = ioctl(g_fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        LOG_E("VIDIOC_DQBUF failed: %s", strerror(errno));
        pthread_mutex_unlock(&g_mutex);
        return V4L2_VIDEO_ERR_DQBUF;
    }

    if (buf.index >= g_buf_count) {
        LOG_E("Invalid buffer index: %u", buf.index);
        ioctl(g_fd, VIDIOC_QBUF, &buf);
        pthread_mutex_unlock(&g_mutex);
        return V4L2_VIDEO_ERR_DQBUF;
    }

    frame->data = g_buffers[buf.index].start;
    frame->length = buf.bytesused;
    frame->width = g_config.width;
    frame->height = g_config.height;
    frame->format = g_config.format;
    frame->timestamp = (uint64_t)buf.timestamp.tv_sec * 1000000 + buf.timestamp.tv_usec;
    frame->index = buf.index;

    LOG_D("Got frame: idx=%u, len=%u, ts=%llu", buf.index, buf.bytesused, frame->timestamp);
    pthread_mutex_unlock(&g_mutex);
    return V4L2_VIDEO_OK;
}

v4l2_video_err_t v4l2_video_put_frame(const v4l2_video_frame_t *frame)
{
    int ret;
    struct v4l2_buffer buf;

    if (frame == NULL) return V4L2_VIDEO_ERR_INVALID_PARAM;
    if (!g_is_init || !g_is_streaming) return V4L2_VIDEO_ERR_NOT_INIT;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = frame->index;

    ret = ioctl(g_fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
        LOG_E("VIDIOC_QBUF failed: %s", strerror(errno));
    }

    LOG_D("Put frame: idx=%u", frame->index);

    if (pthread_mutex_unlock(&g_mutex) != 0) {
        return V4L2_VIDEO_ERR_UNLOCK;
    }

    return V4L2_VIDEO_OK;
}

v4l2_video_err_t v4l2_video_stop(void)
{
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (pthread_mutex_lock(&g_mutex) != 0) return V4L2_VIDEO_ERR_LOCK;
    if (!g_is_init) { pthread_mutex_unlock(&g_mutex); return V4L2_VIDEO_OK; }
    if (!g_is_streaming) { pthread_mutex_unlock(&g_mutex); return V4L2_VIDEO_OK; }

    ret = ioctl(g_fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LOG_E("VIDIOC_STREAMOFF failed: %s", strerror(errno));
        pthread_mutex_unlock(&g_mutex);
        return V4L2_VIDEO_ERR_STREAMOFF;
    }

    g_is_streaming = false;
    LOG_I("V4L2 stream stopped");
    pthread_mutex_unlock(&g_mutex);
    return V4L2_VIDEO_OK;
}

v4l2_video_err_t v4l2_video_deinit(void)
{
    v4l2_video_err_t err = V4L2_VIDEO_OK;

    if (pthread_mutex_lock(&g_mutex) != 0) return V4L2_VIDEO_ERR_LOCK;
    if (!g_is_init) { pthread_mutex_unlock(&g_mutex); return V4L2_VIDEO_OK; }

    if (g_is_streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        err = ioctl(g_fd, VIDIOC_STREAMOFF, &type);
        g_is_streaming = false;
    }

    for (uint32_t i = 0; i < g_buf_count; i++) {
        if (g_buffers[i].start != MAP_FAILED) {
            if (munmap(g_buffers[i].start, g_buffers[i].length) < 0) {
                LOG_E("Munmap buffer %u failed: %s", i, strerror(errno));
            }
            g_buffers[i].start = MAP_FAILED;
        }
    }
    g_buf_count = 0;

    if (g_fd >= 0) {
        if (close(g_fd) < 0) {
            LOG_E("Close device failed: %s", strerror(errno));
        }
        g_fd = -1;
    }

    g_is_init = false;
    memset(&g_config, 0, sizeof(g_config));
    memset(&g_capability, 0, sizeof(g_capability));
    LOG_I("V4L2 video deinit success");

    pthread_mutex_unlock(&g_mutex);
    return err;
}

const char* v4l2_video_err_str(v4l2_video_err_t err)
{
    if (err < 0 || err >= sizeof(g_err_str) / sizeof(g_err_str[0])) {
        return "Unknown error";
    }
    return g_err_str[err];
}

// ==========================================================================
// 【核心新增】一键检测摄像头所有核心能力
// ==========================================================================
static v4l2_video_err_t _v4l2_detect_capability(void)
{
    struct v4l2_fmtdesc fmtdesc;
    uint32_t index = 0;

    LOG_I("========================================");
    LOG_I("Camera Capability Detection Result:");
    LOG_I("========================================");
    LOG_I("Device Name: %s", g_capability.device_name);
    LOG_I("Bus Info: %s", g_capability.bus_info);

    // 1. 检测支持的像素格式
    LOG_I("--- Supported Formats ---");
    while (1) {
        memset(&fmtdesc, 0, sizeof(fmtdesc));
        fmtdesc.index = index;
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(g_fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
            break;
        }

        LOG_I("  [%u] %s (0x%08X)", index, fmtdesc.description, fmtdesc.pixelformat);

        if (fmtdesc.pixelformat == V4L2_PIX_FMT_YUYV) {
            g_capability.support_yuyv = true;
        } else if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
            g_capability.support_mjpeg = true;
        } else if (fmtdesc.pixelformat == V4L2_PIX_FMT_NV12) {
            g_capability.support_nv12 = true;
        }

        index++;
    }

    // 2. 检测AI控制参数支持
    LOG_I("--- AI Control Support ---");
    g_capability.support_manual_exposure = _v4l2_check_control_support(V4L2_CID_EXPOSURE_AUTO);
    g_capability.support_lock_white_balance = _v4l2_check_control_support(V4L2_CID_AUTO_WHITE_BALANCE);
    g_capability.support_lock_gain = _v4l2_check_control_support(V4L2_CID_AUTOGAIN);

    LOG_I("  Manual Exposure: %s", g_capability.support_manual_exposure ? "Yes" : "No");
    LOG_I("  Lock White Balance: %s", g_capability.support_lock_white_balance ? "Yes" : "No");
    LOG_I("  Lock Auto Gain: %s", g_capability.support_lock_gain ? "Yes" : "No");
    LOG_I("========================================");

    return V4L2_VIDEO_OK;
}

// ==========================================================================
// 【辅助】检测某个控制参数是否支持
// ==========================================================================
static bool _v4l2_check_control_support(uint32_t cid)
{
    struct v4l2_queryctrl qctrl;
    memset(&qctrl, 0, sizeof(qctrl));
    qctrl.id = cid;

    if (ioctl(g_fd, VIDIOC_QUERYCTRL, &qctrl) < 0) {
        return false;
    }

    // 检查是否是禁用的控制
    if (qctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
        return false;
    }

    return true;
}

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

static v4l2_video_err_t _v4l2_enum_formats(void)
{
    struct v4l2_fmtdesc fmtdesc;
    uint32_t index = 0;

    LOG_I("Enumerating supported formats:");
    while (1) {
        memset(&fmtdesc, 0, sizeof(fmtdesc));
        fmtdesc.index = index;
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(g_fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
            break;
        }

        LOG_I("  [%u] %s (0x%08X)", index, fmtdesc.description, fmtdesc.pixelformat);
        index++;
    }
    return V4L2_VIDEO_OK;
}

static v4l2_video_err_t _v4l2_set_format(void)
{
    struct v4l2_format fmt;
    int ret;

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = g_config.width;
    fmt.fmt.pix.height = g_config.height;
    fmt.fmt.pix.pixelformat = _v4l2_format_to_fourcc(g_config.format);
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    ret = ioctl(g_fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        LOG_E("VIDIOC_S_FMT failed: %s", strerror(errno));
        return V4L2_VIDEO_ERR_SET_FMT;
    }

    g_config.width = fmt.fmt.pix.width;
    g_config.height = fmt.fmt.pix.height;
    g_config.format = _fourcc_to_v4l2_format(fmt.fmt.pix.pixelformat);

    LOG_I("Set format success: actual %ux%u, fmt=0x%08X",
          g_config.width, g_config.height, fmt.fmt.pix.pixelformat);
    return V4L2_VIDEO_OK;
}

static v4l2_video_err_t _v4l2_alloc_buffers(void)
{
    struct v4l2_requestbuffers req;
    int ret;

    memset(&req, 0, sizeof(req));
    req.count = g_config.buf_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(g_fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        LOG_E("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return V4L2_VIDEO_ERR_REQBUFS;
    }

    g_buf_count = req.count;
    LOG_I("Allocated %u buffers", g_buf_count);
    return V4L2_VIDEO_OK;
}

static v4l2_video_err_t _v4l2_mmap_buffers(void)
{
    struct v4l2_buffer buf;
    int ret;

    for (uint32_t i = 0; i < g_buf_count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(g_fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) {
            LOG_E("VIDIOC_QUERYBUF %u failed: %s", i, strerror(errno));
            return V4L2_VIDEO_ERR_QUERYBUF;
        }

        g_buffers[i].length = buf.length;
        g_buffers[i].start = mmap(NULL, buf.length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED,
                                   g_fd, buf.m.offset);

        if (g_buffers[i].start == MAP_FAILED) {
            LOG_E("Mmap buffer %u failed: %s", i, strerror(errno));
            return V4L2_VIDEO_ERR_MMAP;
        }

        LOG_D("Mmap buffer %u: offset=0x%llX, len=%u",
              i, (unsigned long long)buf.m.offset, buf.length);
    }
    return V4L2_VIDEO_OK;
}

static v4l2_video_err_t _v4l2_queue_all_buffers(void)
{
    struct v4l2_buffer buf;
    int ret;

    for (uint32_t i = 0; i < g_buf_count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(g_fd, VIDIOC_QBUF, &buf);
        if (ret < 0) {
            LOG_E("VIDIOC_QBUF %u failed: %s", i, strerror(errno));
            return V4L2_VIDEO_ERR_QBUF;
        }
    }
    LOG_I("Queued %u buffers", g_buf_count);
    return V4L2_VIDEO_OK;
}

static v4l2_video_err_t _v4l2_lock_ai_params(void)
{
    struct v4l2_control ctl;
    int ret;

    LOG_I("Locking AI parameters (exposure/wb/gain)...");

    // 1. 锁定自动曝光（仅在硬件支持时执行）
    if (g_config.lock_exposure && g_capability.support_manual_exposure) {
        memset(&ctl, 0, sizeof(ctl));
        ctl.id = V4L2_CID_EXPOSURE_AUTO;
        ctl.value = V4L2_EXPOSURE_MANUAL;
        ret = ioctl(g_fd, VIDIOC_S_CTRL, &ctl);
        if (ret < 0) {
            LOG_W("Failed to set manual exposure (non-critical): %s", strerror(errno));
        } else {
            LOG_I("Exposure locked to manual");
        }
    } else if (g_config.lock_exposure && !g_capability.support_manual_exposure) {
        LOG_I("Manual exposure not supported, skipped");
    }

    // 2. 锁定自动白平衡（仅在硬件支持时执行）
    if (g_config.lock_white_balance && g_capability.support_lock_white_balance) {
        memset(&ctl, 0, sizeof(ctl));
        ctl.id = V4L2_CID_AUTO_WHITE_BALANCE;
        ctl.value = 0;
        ret = ioctl(g_fd, VIDIOC_S_CTRL, &ctl);
        if (ret < 0) {
            LOG_W("Failed to lock white balance (non-critical): %s", strerror(errno));
        } else {
            LOG_I("White balance locked");
        }
    } else if (g_config.lock_white_balance && !g_capability.support_lock_white_balance) {
        LOG_I("Lock white balance not supported, skipped");
    }

    // 3. 锁定自动增益（仅在硬件支持时执行，彻底消除警告）
    if (g_config.lock_gain && g_capability.support_lock_gain) {
        memset(&ctl, 0, sizeof(ctl));
        ctl.id = V4L2_CID_AUTOGAIN;
        ctl.value = 0;
        ret = ioctl(g_fd, VIDIOC_S_CTRL, &ctl);
        if (ret < 0) {
            LOG_W("Failed to lock gain (non-critical): %s", strerror(errno));
        } else {
            LOG_I("Gain locked");
        }
    } else if (g_config.lock_gain && !g_capability.support_lock_gain) {
        LOG_I("Lock gain not supported, skipped (no warning)");
    }

    return V4L2_VIDEO_OK;
}

static uint32_t _v4l2_format_to_fourcc(v4l2_video_format_t fmt)
{
    if (fmt == V4L2_PIX_FMT_YUYV) return V4L2_PIX_FMT_YUYV;
    if (fmt == V4L2_PIX_FMT_NV12) return V4L2_PIX_FMT_NV12;
    if (fmt == V4L2_PIX_FMT_MJPEG) return V4L2_PIX_FMT_MJPEG;
    return V4L2_PIX_FMT_YUYV;
}

static v4l2_video_format_t _fourcc_to_v4l2_format(uint32_t fourcc)
{
    if (fourcc == V4L2_PIX_FMT_YUYV) return V4L2_PIX_FMT_YUYV;
    if (fourcc == V4L2_PIX_FMT_NV12) return V4L2_PIX_FMT_NV12;
    if (fourcc == V4L2_PIX_FMT_MJPEG) return V4L2_PIX_FMT_MJPEG;
    return V4L2_PIX_FMT_YUYV;
}
