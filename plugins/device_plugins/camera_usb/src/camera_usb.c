// V3.0 严格路径：包含自身头文件
#include "../inc/camera_usb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/**
 * @brief USB摄像头 私有子类结构体
 * V3.0 强制规则：
 * 1. 基类 struct camera_base 必须是**第一个成员**
 * 2. 仅存放硬件私有参数：fd、设备路径、缓冲区等
 * 3. static 修饰，对外完全隐藏（信息隐藏）
 */
typedef struct {
    camera_base_t  base;          /* 基类：固定首位 */
    int            fd;            /* 设备文件描述符 */
    const char    *dev_path;      /* 设备节点路径 */
    void          *buf[4];        /* MMAP缓冲区 */
    size_t         buf_len[4];    /* 缓冲区长度 */
    int            buf_cnt;       /* 缓冲区数量 */
} camera_usb_t;

/* ============================================================================
 * 私有静态实现函数（V3.0 强制：仅本文件可见，禁止外部访问）
 * ========================================================================== */
/**
 * @brief USB摄像头初始化
 * @param base_me 基类指针
 * @return 0成功，负数失败
 */
static int camera_usb_init(camera_base_t *base_me)
{
    if (!base_me)
        return -EINVAL;
    
    // V3.0 强制：向下转型仅使用 container_of，禁止裸指针强转
    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    int ret, i;

    // 1. 打开设备
    me->fd = v4l2_open(me->dev_path);
    if (me->fd < 0) {
        perror("camera_usb open failed");
        return -errno;
    }

    // 2. 设置视频格式
    ret = v4l2_set_format(me->fd, me->base.width, me->base.height);
    if (ret < 0) {
        perror("camera_usb set format failed");
        goto err_close;
    }

    // 3. 申请V4L2缓冲区
    me->buf_cnt = 4;
    ret = v4l2_reqbufs(me->fd, me->buf_cnt);
    if (ret < 0) {
        perror("camera_usb reqbufs failed");
        goto err_close;
    }

    // 4. 查询+映射+入队 所有缓冲区
    for (i = 0; i < me->buf_cnt; i++) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        ret = v4l2_querybuf(me->fd, &buf);
        if (ret < 0) {
            perror("camera_usb querybuf failed");
            goto err_munmap;
        }

        me->buf[i] = v4l2_mmap(me->fd, buf.length, buf.m.offset);
        me->buf_len[i] = buf.length;
        if (me->buf[i] == MAP_FAILED) {
            perror("camera_usb mmap failed");
            goto err_munmap;
        }

        // 缓冲区入队（V4L2必需流程）
        ret = v4l2_qbuf(me->fd, &buf);
        if (ret < 0) {
            perror("camera_usb qbuf failed");
            goto err_munmap;
        }
    }

    printf("[USB Camera] init success: %s, %dx%d\n",
           me->dev_path, me->base.width, me->base.height);
    return 0;

    // 错误处理：释放已申请的资源
err_munmap:
    for (int j = 0; j < i; j++) {
        v4l2_munmap(me->buf[j], me->buf_len[j]);
    }
err_close:
    v4l2_close(me->fd);
    me->fd = -1;
    return ret;
}

/**
 * @brief USB摄像头反初始化（资源释放）
 */
static int camera_usb_deinit(camera_base_t *base_me)
{
    if (!base_me)
        return -EINVAL;

    camera_usb_t *me = container_of(base_me, camera_usb_t, base);

    // 安全停止采集
    camera_stop_capture(base_me);

    // 释放MMAP缓冲区
    for (int i = 0; i < me->buf_cnt; i++) {
        v4l2_munmap(me->buf[i], me->buf_len[i]);
        me->buf[i] = NULL;
    }

    // 关闭设备
    v4l2_close(me->fd);
    me->fd = -1;

    printf("[USB Camera] deinit success\n");
    return 0;
}

/**
 * @brief 启动视频采集
 */
static int camera_usb_start_capture(camera_base_t *base_me)
{
    if (!base_me)
        return -EINVAL;

    camera_usb_t *me = container_of(base_me, camera_usb_t, base);

    // 防护：未初始化禁止启动
    if (me->fd < 0)
        return -ENODEV;

    int ret = v4l2_stream_ctrl(me->fd, true);
    if (ret < 0) {
        perror("camera_usb stream on failed");
        return ret;
    }

    printf("[USB Camera] start capture success\n");
    return 0;
}

/**
 * @brief 停止视频采集
 */
static int camera_usb_stop_capture(camera_base_t *base_me)
{
    if (!base_me)
        return -EINVAL;

    camera_usb_t *me = container_of(base_me, camera_usb_t, base);

    if (me->fd < 0)
        return -ENODEV;

    int ret = v4l2_stream_ctrl(me->fd, false);
    if (ret < 0) {
        perror("camera_usb stream off failed");
        return ret;
    }

    printf("[USB Camera] stop capture success\n");
    return 0;
}

/**
 * @brief 获取一帧图像
 */
static int camera_usb_get_frame(camera_base_t *base_me, void **frame, size_t *len)
{
    if (!base_me || !frame || !len)
        return -EINVAL;

    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    if (me->fd < 0 || !me->base.is_running)
        return -ENODEV;

    struct v4l2_buffer buf = {0};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // 阻塞模式：直接调用，100%成功返回
    int ret = v4l2_dqbuf(me->fd, &buf);
    if (ret < 0)
        return ret;

    *frame = me->buf[buf.index];
    *len = me->buf_len[buf.index];
    v4l2_qbuf(me->fd, &buf);
    
    return 0;
}

/**
 * @brief 设置摄像头参数
 */
/**
 * @brief 设置摄像头参数 (V3.0 热配置实现)
 * 支持：采集运行中 实时修改参数（热配置）
 */
static int camera_usb_set_param(camera_base_t *base_me, int cmd, void *arg)
{
    if (!base_me || !arg)
        return -EINVAL;

    camera_usb_t *me = container_of(base_me, camera_usb_t, base);
    // 热配置前提：设备必须已初始化（运行/停止状态都能改，真正热配置）
    if (me->fd < 0)
        return -ENODEV;

    int *val = (int *)arg;
    struct v4l2_control ctrl = {0};

    switch (cmd) {
        case CAMERA_PARAM_SET_FPS:
            // 备注：FPS 属于格式参数，UVC 摄像头需停止流后修改（半热配置）
            printf("[USB Camera] set fps: %d (需重启流生效)\n", *val);
            break;

        case CAMERA_PARAM_SET_EXPOSURE:
            // V4L2 曝光控件 (UVC 通用) → 【热配置，实时生效】
            ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
            ctrl.value = *val;
            if (ioctl(me->fd, VIDIOC_S_CTRL, &ctrl) < 0) {
                perror("[USB Camera] set exposure failed");
                return -errno;
            }
            printf("[USB Camera] set exposure: %d (热配置生效)\n", *val);
            break;

        case CAMERA_PARAM_SET_BRIGHTNESS:
            // V4L2 亮度控件 (UVC 通用) → 【热配置，实时生效】
            ctrl.id = V4L2_CID_BRIGHTNESS;
            ctrl.value = *val;
            if (ioctl(me->fd, VIDIOC_S_CTRL, &ctrl) < 0) {
                perror("[USB Camera] set brightness failed");
                return -errno;
            }
            printf("[USB Camera] set brightness: %d (热配置生效)\n", *val);
            break;

        default:
            return -ENOSYS; // 不支持的命令
    }

    return 0;
}

/* ============================================================================
 * V3.0 强制：const OPS操作表（存入只读段，防篡改）
 * ========================================================================== */
static const camera_ops_t g_camera_usb_ops = {
    .init          = camera_usb_init,
    .deinit        = camera_usb_deinit,
    .start_capture = camera_usb_start_capture,
    .stop_capture  = camera_usb_stop_capture,
    .get_frame     = camera_usb_get_frame,
    .set_param     = camera_usb_set_param,
};

/* ============================================================================
 * 对外构造/析构函数（唯一对外接口）
 * ========================================================================== */
/**
 * @brief USB摄像头对象创建
 */
camera_base_t *camera_usb_create(const char *dev_path, int width, int height)
{
    // 参数合法性校验
    if (!dev_path || width <= 0 || height <= 0) {
        printf("[USB Camera] create param invalid\n");
        return NULL;
    }

    // 分配内存（清零）
    camera_usb_t *me = (camera_usb_t *)calloc(1, sizeof(camera_usb_t));
    if (!me) {
        perror("[USB Camera] malloc failed");
        return NULL;
    }

    // 基类初始化
    me->base.ops    = &g_camera_usb_ops;
    me->base.name   = "usb_camera";
    me->base.width  = width;
    me->base.height = height;
    me->base.is_init = false;
    me->base.is_running = false;

    // 私有成员初始化
    me->dev_path = dev_path;
    me->fd       = -1;

    // 返回基类指针（向上转型，符合OOP多态）
    return &me->base;
}

/**
 * @brief USB摄像头对象销毁
 */
void camera_usb_destroy(camera_base_t *base_me)
{
    if (!base_me)
        return;

    camera_usb_t *me = container_of(base_me, camera_usb_t, base);

    // 反初始化 + 释放内存
    camera_deinit(base_me);
    free(me);
    printf("[USB Camera] destroy success\n");
}