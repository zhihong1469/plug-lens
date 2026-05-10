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
/* USB 摄像头最大缓冲区数 */
#define CAMERA_USB_MAX_BUF      4

/**
 * @brief USB 摄像头私有子类（完全封装，对外不可见）
 * V3.0 规则：基类必须放在第一个成员
 */
typedef struct {
    camera_base_t      base;
    int                fd;
    const char        *dev_path;
    uint32_t           pixel_fmt;
    camera_capability_t cap;     /* 设备能力（全自检结果） */

    void               *buf[CAMERA_USB_MAX_BUF];
    size_t              buf_len[CAMERA_USB_MAX_BUF];
    int                 buf_cnt;
} camera_usb_t;

/* ============================================================================
 * 私有工具函数
 * ========================================================================== */
static int camera_usb_check_format_support(camera_usb_t *me)
{
    if (!me->cap.support_yuyv && me->pixel_fmt == V4L2_PIX_FMT_YUYV) {
        printf("[USB Camera] YUYV 格式不支持\n");
        return -ENOTSUP;
    }
    if (!me->cap.support_mjpeg && me->pixel_fmt == V4L2_PIX_FMT_MJPEG) {
        printf("[USB Camera] MJPEG 格式不支持\n");
        return -ENOTSUP;
    }
    if (!me->cap.support_nv12 && me->pixel_fmt == V4L2_PIX_FMT_NV12) {
        printf("[USB Camera] NV12 格式不支持\n");
        return -ENOTSUP;
    }
    return 0;
}

/* ============================================================================
 * 子类 OPS 实现（全自检版）
 * ========================================================================== */
static int camera_usb_init(camera_base_t *base_me)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    int ret, i;

    // ====================== 【自检 1】打开设备 ======================
    me->fd = v4l2_open(me->dev_path);
    if (me->fd < 0) {
        perror("[USB Camera] open failed");
        return -errno;
    }

    // ====================== 【自检 2】查询设备基础能力 ======================
    ret = v4l2_query_capability(me->fd, &me->cap);
    if (ret < 0) {
        printf("[USB Camera] 设备能力查询失败\n");
        goto err_close;
    }

    // ====================== 【自检 3】枚举所有支持格式 ======================
    ret = v4l2_enum_formats(me->fd, &me->cap);
    if (ret < 0) {
        printf("[USB Camera] 格式枚举失败\n");
        goto err_close;
    }

    // ====================== 【自检 4】检查目标格式是否支持 ======================
    ret = camera_usb_check_format_support(me);
    if (ret < 0)
        goto err_close;

    // ====================== 【自检 5】设置格式 + 回读校验 ======================
    int w = me->base.width;
    int h = me->base.height;
    ret = v4l2_set_format(me->fd, &w, &h, me->pixel_fmt);
    if (ret < 0) {
        perror("[USB Camera] set format failed");
        goto err_close;
    }
    me->base.width = w;
    me->base.height = h;

    // ====================== 【自检 6】设置帧率 + 回读 ======================
    uint32_t fps = me->base.fps;
    ret = v4l2_set_fps(me->fd, &fps);
    if (ret < 0) {
        printf("[USB Camera] set fps warning\n");
    }
    me->base.fps = fps;

    // ====================== 【自检 7】申请缓冲区 ======================
    me->buf_cnt = CAMERA_USB_MAX_BUF;
    ret = v4l2_reqbufs(me->fd, &me->buf_cnt);
    if (ret < 0) {
        perror("[USB Camera] reqbufs failed");
        goto err_close;
    }

    // ====================== 【自检 8】MMAP 映射 ======================
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

        // 入队
        ret = v4l2_qbuf(me->fd, &buf);
        if (ret < 0) {
            perror("[USB Camera] qbuf failed");
            goto err_munmap;
        }
    }

    printf("[USB Camera] 初始化全自检完成 ✅\n");
    return 0;

err_munmap:
    for (int j = 0; j < i; j++)
        v4l2_munmap(me->buf[j], me->buf_len[j]);
err_close:
    v4l2_close(me->fd);
    me->fd = -1;
    return ret;
}

static int camera_usb_deinit(camera_base_t *base_me)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);

    camera_stop_capture(base_me);

    for (int i = 0; i < me->buf_cnt; i++)
        v4l2_munmap(me->buf[i], me->buf_len[i]);

    v4l2_close(me->fd);
    me->fd = -1;

    printf("[USB Camera] 反初始化完成\n");
    return 0;
}

static int camera_usb_start_capture(camera_base_t *base_me)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    if (me->fd < 0) return -ENODEV;

    int ret = v4l2_stream_ctrl(me->fd, true);
    if (ret < 0) {
        perror("[USB Camera] stream on failed");
        return ret;
    }

    printf("[USB Camera] 开始采集\n");
    return 0;
}

static int camera_usb_stop_capture(camera_base_t *base_me)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    if (me->fd < 0) return -ENODEV;

    v4l2_stream_ctrl(me->fd, false);
    printf("[USB Camera] 停止采集\n");
    return 0;
}

static int camera_usb_get_frame(camera_base_t *base_me, void **frame, size_t *len)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    if (me->fd < 0 || !base_me->is_running) return -ENODEV;

    struct v4l2_buffer buf = {0};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    int ret = v4l2_dqbuf(me->fd, &buf);
    if (ret < 0) return ret;

    *frame = me->buf[buf.index];
    *len = me->buf_len[buf.index];

    v4l2_qbuf(me->fd, &buf);
    return 0;
}

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
            printf("[USB Camera] 曝光 = %d\n", *val);
            return ret;

        case CAMERA_PARAM_SET_BRIGHTNESS:
            ret = v4l2_set_ctrl(me->fd, V4L2_CID_BRIGHTNESS, *val);
            printf("[USB Camera] 亮度 = %d\n", *val);
            return ret;

        case CAMERA_PARAM_SET_WHITE_BALANCE:
            if (!me->cap.support_white_balance) return -ENOTSUP;
            v4l2_set_ctrl(me->fd, V4L2_CID_AUTO_WHITE_BALANCE, 0);
            ret = v4l2_set_ctrl(me->fd, V4L2_CID_WHITE_BALANCE_TEMPERATURE, *val);
            printf("[USB Camera] 白平衡 = %d\n", *val);
            return ret;

        default:
            return -ENOSYS;
    }
}

static int camera_usb_get_capability(camera_base_t *base_me, camera_capability_t *cap)
{
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    memcpy(cap, &me->cap, sizeof(*cap));
    return 0;
}

/* ============================================================================
 * 虚函数表（V3.0 必须 const）
 * ========================================================================== */
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
 * 对外构造 / 析构
 * ========================================================================== */
camera_base_t *camera_usb_create(const char *dev_path,
                                 int width,
                                 int height,
                                 uint32_t fmt,
                                 uint32_t fps)
{
    if (!dev_path || width <= 0 || height <= 0)
        return NULL;

    camera_usb_t *me = calloc(1, sizeof(*me));
    if (!me) return NULL;

    me->base.ops        = &g_camera_usb_ops;
    me->base.name       = "usb_camera";
    me->base.width      = width;
    me->base.height     = height;
    me->base.fps        = fps;
    me->base.is_init    = false;
    me->base.is_running = false;

    me->dev_path    = dev_path;
    me->pixel_fmt   = fmt;
    me->fd          = -1;

    return &me->base;
}

void camera_usb_destroy(camera_base_t *base_me)
{
    if (!base_me) return;
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    camera_deinit(base_me);
    free(me);
    printf("[USB Camera] 销毁成功\n");
}