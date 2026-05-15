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
 * @file camera_base.h
 * @brief 摄像头设备抽象基类头文件（V3.0 架构版）
 * @details 定义摄像头通用抽象接口、能力结构体、V4L2底层封装
 *          所有摄像头子类（USB/CSI等）必须继承该基类实现
 *          核心特性：硬件缓冲区管理内部闭环，上层无需要手动释放
 * @version 3.0
 * @date 2025
 */

/**
 * @brief container_of 宏：V3.0 强制向下转型方案
 * @details 禁止裸指针强制转换，仅通过该宏实现基类→子类的安全转换
 *          嵌入式面向对象C语言核心宏
 * @param ptr 基类指针
 * @param type 子类结构体类型
 * @param member 子类中基类的成员名
 */
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

/* 前置声明：摄像头基类 */
typedef struct camera_base camera_base_t;

/**
 * @brief 摄像头硬件能力检测结构体
 * @details 通用设备能力描述，子类初始化时自动填充
 *          用于上层查询设备支持的格式、参数等特性
 */
typedef struct {
    char device_name[32];    /**< 设备名称（如：USB Camera） */
    char bus_info[32];       /**< 总线信息（如：usb-ci_hdrc.1-1） */
    bool support_yuyv;       /**< 是否支持YUYV像素格式 */
    bool support_mjpeg;      /**< 是否支持MJPEG像素格式 */
    bool support_nv12;       /**< 是否支持NV12像素格式 */
    bool support_exposure;   /**< 是否支持手动曝光调节 */
    bool support_white_balance; /**< 是否支持手动白平衡调节 */
    bool support_gain;       /**< 是否支持手动增益调节 */
} camera_capability_t;

/**
 * @brief 摄像头操作函数表（OPS）
 * @details V3.0 强制规则：
 *          1. 仅存储函数指针，const 实例化在 .c 文件中
 *          2. 所有子类必须实现全部接口
 *          3. 纯硬件操作，无业务逻辑
 */
typedef struct {
    /** 设备初始化（含自检、格式配置、缓冲区申请） */
    int (*init)(camera_base_t *me);
    /** 设备反初始化（释放资源、关闭设备） */
    int (*deinit)(camera_base_t *me);
    /** 启动视频流采集 */
    int (*start_capture)(camera_base_t *me);
    /** 停止视频流采集 */
    int (*stop_capture)(camera_base_t *me);
    /** 获取一帧视频数据 */
    int (*get_frame)(camera_base_t *me, void **frame, size_t *len);
    /** 设置摄像头参数（曝光/亮度/白平衡等） */
    int (*set_param)(camera_base_t *me, int cmd, void *arg);
    /** 获取摄像头硬件能力信息 */
    int (*get_capability)(camera_base_t *me, camera_capability_t *cap);
} camera_ops_t;

/**
 * @brief 摄像头基类结构体
 * @details V3.0 强制规则：
 *          1. const ops 必须为第一个成员
 *          2. 仅存放公共属性，无硬件私有成员
 *          3. 纯设备抽象，无业务逻辑
 */
struct camera_base {
    const camera_ops_t *ops;  /**< 固定首位：只读操作函数表 */
    const char *name;         /**< 设备名称（usb_camera/csi_camera） */
    int width;                /**< 实际生效的图像宽度 */
    int height;               /**< 实际生效的图像高度 */
    uint32_t fps;             /**< 实际生效的帧率 */
    bool is_running;          /**< 采集运行状态：true-运行 false-停止 */
    bool is_init;             /**< 初始化状态：true-已初始化 false-未初始化 */
};

/**
 * @brief 摄像头公共参数配置命令枚举
 * @details 上层调用 camera_set_param 时使用的命令字
 */
enum camera_param_cmd {
    CAMERA_PARAM_SET_FPS,             /**< 设置帧率 */
    CAMERA_PARAM_SET_EXPOSURE,        /**< 设置手动曝光 */
    CAMERA_PARAM_SET_BRIGHTNESS,      /**< 设置亮度 */
    CAMERA_PARAM_SET_WHITE_BALANCE,   /**< 设置手动白平衡 */
    CAMERA_PARAM_SET_GAIN,            /**< 设置手动增益 */
};

/* ============================================================================
 * @brief V4L2 系统调用薄封装（设备驱动层专用）
 * @details 无业务逻辑，仅封装Linux V4L2原生接口
 *          所有摄像头子类通用，禁止上层直接调用
 * ========================================================================== */
// 基础设备操作
int v4l2_open(const char *dev_path);                /**< 打开V4L2设备 */
void v4l2_close(int fd);                            /**< 关闭V4L2设备 */
int v4l2_stream_ctrl(int fd, bool on);              /**< 开启/关闭视频流 */

// 硬件自检核心函数
int v4l2_query_capability(int fd, camera_capability_t *cap);  /**< 查询设备基础能力 */
int v4l2_check_control_support(int fd, uint32_t cid);        /**< 检查参数是否支持 */
int v4l2_enum_formats(int fd, camera_capability_t *cap);      /**< 枚举支持的像素格式 */

// 图像格式配置
int v4l2_set_format(int fd, int *width, int *height, uint32_t pixelformat); /**< 设置图像格式 */
int v4l2_get_format(int fd, int *width, int *height, uint32_t *pixelformat); /**< 获取当前格式 */
int v4l2_set_fps(int fd, uint32_t *fps);                                    /**< 设置并回读帧率 */

// 缓冲区管理
int v4l2_reqbufs(int fd, int *buf_cnt);                 /**< 申请V4L2内核缓冲区 */
int v4l2_querybuf(int fd, struct v4l2_buffer *buf);     /**< 查询缓冲区信息 */
int v4l2_qbuf(int fd, struct v4l2_buffer *buf);        /**< 缓冲区入队（归还内核） */
int v4l2_dqbuf(int fd, struct v4l2_buffer *buf);       /**< 缓冲区出队（从内核获取） */
void *v4l2_mmap(int fd, size_t length, off_t offset);  /**< 内存映射 */
void v4l2_munmap(void *addr, size_t length);           /**< 解除内存映射 */

// 参数控制
int v4l2_set_ctrl(int fd, uint32_t cid, int value);    /**< 设置V4L2控制参数 */
int v4l2_get_ctrl(int fd, uint32_t cid, int *value);  /**< 获取V4L2控制参数 */

/* ============================================================================
 * @brief 摄像头基类对外统一接口（V3.0 强制校验+分发）
 * @details 上层业务模块唯一调用接口，自动校验参数+分发到子类实现
 * ========================================================================== */
/**
 * @brief 初始化摄像头设备
 * @param me 摄像头基类指针
 * @return 0成功 负数失败
 */
int camera_init(camera_base_t *me);

/**
 * @brief 反初始化摄像头设备
 * @param me 摄像头基类指针
 * @return 0成功 负数失败
 */
int camera_deinit(camera_base_t *me);

/**
 * @brief 启动摄像头采集
 * @param me 摄像头基类指针
 * @return 0成功 负数失败
 */
int camera_start_capture(camera_base_t *me);

/**
 * @brief 停止摄像头采集
 * @param me 摄像头基类指针
 * @return 0成功 负数失败
 */
int camera_stop_capture(camera_base_t *me);

/**
 * @brief 获取一帧视频数据
 * @param me 摄像头基类指针
 * @param frame 输出：帧数据指针
 * @param len 输出：帧数据长度
 * @return 0成功 负数失败
 * @note 【核心重要】内部已完成V4L2缓冲区 dqbuf+qbuf 闭环
 *       硬件缓冲区自动回收，上层**无需、禁止**手动释放！
 */
int camera_get_frame(camera_base_t *me, void **frame, size_t *len);

/**
 * @brief 设置摄像头参数
 * @param me 摄像头基类指针
 * @param cmd 参数命令（enum camera_param_cmd）
 * @param arg 参数值指针
 * @return 0成功 负数失败
 */
int camera_set_param(camera_base_t *me, int cmd, void *arg);

/**
 * @brief 获取摄像头硬件能力信息
 * @param me 摄像头基类指针
 * @param cap 输出：能力信息结构体
 * @return 0成功 负数失败
 */
int camera_get_capability(camera_base_t *me, camera_capability_t *cap);

#ifdef __cplusplus
}
#endif

#endif /* __CAMERA_BASE_H__ */