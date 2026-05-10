#include "camera_usb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// USB子类（私有结构体，对外完全隐藏）
typedef struct {
    camera_base_t base;
    int fd;
    const char *dev_path;
    void *buf[4];
    size_t buf_len[4];
    int buf_cnt;
} camera_usb_t;

// 静态私有实现（仅本文件可见）
static int camera_usb_init(camera_base_t *base_me) {
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    int ret, i;

    me->fd = v4l2_open(me->dev_path);
    if (me->fd < 0) {
        perror("usb camera open failed");
        return -errno;
    }

    ret = v4l2_set_format(me->fd, me->base.width, me->base.height);
    if (ret < 0) goto err_close;

    me->buf_cnt = 4;
    ret = v4l2_reqbufs(me->fd, me->buf_cnt);
    if (ret < 0) goto err_close;

    // 映射所有缓冲区 + 入队
    for (i = 0; i < me->buf_cnt; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        ret = v4l2_querybuf(me->fd, &buf);
        if (ret < 0) goto err_munmap;

        me->buf[i] = v4l2_mmap(me->fd, buf.length, buf.m.offset);
        me->buf_len[i] = buf.length;
        if (me->buf[i] == MAP_FAILED) goto err_munmap;

        // 缓冲区入队（V4L2必须操作）
        ret = v4l2_qbuf(me->fd, &buf);
        if (ret < 0) goto err_munmap;
    }

    printf("USB Camera: %s 初始化成功 %dx%d\n", me->dev_path, me->base.width, me->base.height);
    return 0;

err_munmap:
    for (int j = 0; j < i; j++) v4l2_munmap(me->buf[j], me->buf_len[j]);
err_close:
    v4l2_close(me->fd);
    me->fd = -1;
    return -errno;
}

static int camera_usb_deinit(camera_base_t *base_me) {
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);

    camera_stop_capture(base_me);
    for (int i = 0; i < me->buf_cnt; i++) {
        v4l2_munmap(me->buf[i], me->buf_len[i]);
    }
    v4l2_close(me->fd);
    me->fd = -1;

    printf("USB Camera: 反初始化成功\n");
    return 0;
}

static int camera_usb_start_capture(camera_base_t *base_me) {
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    int ret = v4l2_stream_ctrl(me->fd, true);
    if (ret < 0) return -errno;
    printf("USB Camera: 启动采集\n");
    return 0;
}

static int camera_usb_stop_capture(camera_base_t *base_me) {
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    int ret = v4l2_stream_ctrl(me->fd, false);
    if (ret < 0) return -errno;
    printf("USB Camera: 停止采集\n");
    return 0;
}

static int camera_usb_get_frame(camera_base_t *base_me, void **frame, size_t *len) {
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    struct v4l2_buffer buf = {0};
    int ret;

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // 出队获取帧
    ret = v4l2_dqbuf(me->fd, &buf);
    if (ret < 0) return -errno;

    *frame = me->buf[buf.index];
    *len = me->buf_len[buf.index];

    // 重新入队，循环使用
    v4l2_qbuf(me->fd, &buf);
    return 0;
}

static int camera_usb_set_param(camera_base_t *base_me, int cmd, void *arg) {
    int *val = arg;
    switch (cmd) {
        case CAMERA_PARAM_SET_FPS: printf("USB Camera: 设置帧率 %d\n", *val); break;
        case CAMERA_PARAM_SET_EXPOSURE: printf("USB Camera: 设置曝光 %d\n", *val); break;
        default: return -ENOSYS;
    }
    return 0;
}

// Const OPS表（只读段，防篡改）
static const camera_ops_t camera_usb_ops = {
    .init          = camera_usb_init,
    .deinit        = camera_usb_deinit,
    .start_capture = camera_usb_start_capture,
    .stop_capture  = camera_usb_stop_capture,
    .get_frame     = camera_usb_get_frame,
    .set_param     = camera_usb_set_param,
};

// 构造函数
camera_base_t *camera_usb_create(const char *dev_path, int width, int height) {
    if (!dev_path || width <= 0 || height <= 0) return NULL;

    camera_usb_t *me = calloc(1, sizeof(camera_usb_t));
    if (!me) return NULL;

    // 基类初始化
    me->base.ops = &camera_usb_ops;
    me->base.name = "usb_camera";
    me->base.width = width;
    me->base.height = height;
    // 私有成员初始化
    me->dev_path = dev_path;
    me->fd = -1;

    return &me->base;
}

// 析构函数
void camera_usb_destroy(camera_base_t *base_me) {
    if (!base_me) return;
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    camera_deinit(base_me);
    free(me);
}