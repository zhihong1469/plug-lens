#ifndef __CAMERA_BASE_H__
#define __CAMERA_BASE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * container_of 宏：V3.0 强制向下转型方案
 * 禁止裸指针强转，仅使用该宏进行子类/基类转换
 */
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

/* 前置声明 */
typedef struct camera_base camera_base_t;

/**
 * 摄像头OPS操作表
 * V3.0 规则：仅存放函数指针，const实例化在.c中
 */
typedef struct {
    int (*init)(camera_base_t *me);
    int (*deinit)(camera_base_t *me);
    int (*start_capture)(camera_base_t *me);
    int (*stop_capture)(camera_base_t *me);
    int (*get_frame)(camera_base_t *me, void **frame, size_t *len);
    int (*set_param)(camera_base_t *me, int cmd, void *arg);
} camera_ops_t;

/**
 * 摄像头基类结构体
 * V3.0 强制规则：
 * 1. const ops 为第一个成员
 * 2. 仅存放公共属性，无硬件私有成员
 * 3. 无业务逻辑，纯设备抽象
 */
struct camera_base {
    const camera_ops_t *ops;  /* 固定首位：只读OPS表 */
    const char *name;         /* 设备名称 */
    int width;                /* 分辨率宽 */
    int height;               /* 分辨率高 */
    bool is_running;          /* 采集状态 */
    bool is_init;             /* 初始化状态 */
};

/**
 * 摄像头参数命令枚举
 * 公共参数，对外暴露
 */
enum camera_param_cmd {
    CAMERA_PARAM_SET_FPS,
    CAMERA_PARAM_SET_EXPOSURE,
    CAMERA_PARAM_SET_BRIGHTNESS,
};

/* ============================================================================
 * V4L2 系统调用薄封装（Device层专用，无业务逻辑）
 * ========================================================================== */
int v4l2_open(const char *dev_path);
void v4l2_close(int fd);
int v4l2_set_format(int fd, int width, int height);
int v4l2_reqbufs(int fd, int buf_cnt);
int v4l2_querybuf(int fd, struct v4l2_buffer *buf);
int v4l2_qbuf(int fd, struct v4l2_buffer *buf);
int v4l2_dqbuf(int fd, struct v4l2_buffer *buf);
void *v4l2_mmap(int fd, size_t length, off_t offset);
void v4l2_munmap(void *addr, size_t length);
int v4l2_stream_ctrl(int fd, bool on);

/* ============================================================================
 * 摄像头基类对外统一接口（V3.0 强制校验+分发）
 * ========================================================================== */
int camera_init(camera_base_t *me);
int camera_deinit(camera_base_t *me);
int camera_start_capture(camera_base_t *me);
int camera_stop_capture(camera_base_t *me);
int camera_get_frame(camera_base_t *me, void **frame, size_t *len);
int camera_set_param(camera_base_t *me, int cmd, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* __CAMERA_BASE_H__ */