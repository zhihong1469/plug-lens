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
 * 摄像头能力检测结果（来自老代码精华）
 * 所有摄像头通用，子类初始化时填充
 */
typedef struct {
    char device_name[32];    // 设备名称
    char bus_info[32];       // 总线信息
    bool support_yuyv;       // 支持YUYV格式
    bool support_mjpeg;      // 支持MJPEG格式
    bool support_nv12;       // 支持NV12格式
    bool support_exposure;   // 支持手动曝光
    bool support_white_balance; // 支持手动白平衡
    bool support_gain;       // 支持手动增益
} camera_capability_t;

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
    int (*get_capability)(camera_base_t *me, camera_capability_t *cap); // 新增：获取能力
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
    int width;                /* 实际生效分辨率宽 */
    int height;               /* 实际生效分辨率高 */
    uint32_t fps;             /* 实际生效帧率 */
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
    CAMERA_PARAM_SET_WHITE_BALANCE,
    CAMERA_PARAM_SET_GAIN,
};

/* ============================================================================
 * V4L2 系统调用薄封装（Device层专用，无业务逻辑）
 * 集成老代码核心自检逻辑，所有子类通用
 * ========================================================================== */
// 基础操作
int v4l2_open(const char *dev_path);
void v4l2_close(int fd);
int v4l2_stream_ctrl(int fd, bool on);

// 核心自检函数（来自老代码精华）
int v4l2_query_capability(int fd, camera_capability_t *cap);
int v4l2_check_control_support(int fd, uint32_t cid);
int v4l2_enum_formats(int fd, camera_capability_t *cap);

// 格式与参数操作（带回读校验）
int v4l2_set_format(int fd, int *width, int *height, uint32_t pixelformat);
int v4l2_get_format(int fd, int *width, int *height, uint32_t *pixelformat);
int v4l2_set_fps(int fd, uint32_t *fps);

// 缓冲区操作
int v4l2_reqbufs(int fd, int *buf_cnt);
int v4l2_querybuf(int fd, struct v4l2_buffer *buf);
int v4l2_qbuf(int fd, struct v4l2_buffer *buf);
int v4l2_dqbuf(int fd, struct v4l2_buffer *buf);
void *v4l2_mmap(int fd, size_t length, off_t offset);
void v4l2_munmap(void *addr, size_t length);

// 控制参数操作（热配置）
int v4l2_set_ctrl(int fd, uint32_t cid, int value);
int v4l2_get_ctrl(int fd, uint32_t cid, int *value);

/* ============================================================================
 * 摄像头基类对外统一接口（V3.0 强制校验+分发）
 * ========================================================================== */
int camera_init(camera_base_t *me);
int camera_deinit(camera_base_t *me);
int camera_start_capture(camera_base_t *me);
int camera_stop_capture(camera_base_t *me);
int camera_get_frame(camera_base_t *me, void **frame, size_t *len);
int camera_set_param(camera_base_t *me, int cmd, void *arg);
int camera_get_capability(camera_base_t *me, camera_capability_t *cap); // 新增：获取能力

#ifdef __cplusplus
}
#endif

#endif /* __CAMERA_BASE_H__ */