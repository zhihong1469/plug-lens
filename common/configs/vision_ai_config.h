#ifndef VISION_AI_CONFIG_H
#define VISION_AI_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 一、全局总线名称（全系统统一，服务禁止硬编码！）
// 匹配采集服务 + 人脸检测服务 纯DataBus架构
// ==========================================================================
#define SYS_EVENT_BUS_NAME        "sys_event"      // 系统事件总线
#define SYS_DATA_BUS_NAME         "sys_data"       // 系统数据总线
#define VIDEO_DATA_BUS_NAME       "video"          // 摄像头YUYV视频总线（采集服务生产）
#define AI_RGB_DATA_BUS_NAME      "ai_rgb"         // AI专属RGB数据总线（人脸服务生产）
#define FACE_YUV_DATA_BUS_NAME    "face_result"    // 人脸检测结果数据总线
// ==========================================================================
// 【全局通用事件类型】（0x0000-0x0FFF）
// 所有服务都需要订阅的系统级控制事件，仅此一处定义
// 区间规划：
// 0x0000 ~ 0x0FFF：系统全局事件
// 0x1000 ~ 0x1FFF：模块通用基础事件
// 0x2000 ~ 0x2FFF：视频采集服务专属事件
// 0x3000 ~ 0x3FFF：人脸检测服务专属事件
// 0x4000 ~ 0x4FFF：Demo应用服务专属事件
// ==========================================================================
typedef enum {
    // ======================================
    // 系统全局事件 (0x0000 ~ 0x0FFF)
    // ======================================
    EVENT_TYPE_INVALID            = 0x0000,
    EVENT_TYPE_SYS_BASE           = 0x0001,
    EVENT_TYPE_SYS_CORE_READY,     // 双总线初始化完成
    EVENT_TYPE_SYS_PAUSE,          // 全局暂停
    EVENT_TYPE_SYS_RESUME,         // 全局恢复
    EVENT_TYPE_SYS_STOP,           // 全局停止
    EVENT_TYPE_SYS_SHUTDOWN,       // 系统关机
    EVENT_TYPE_SYS_ERROR,          // 系统错误
    EVENT_TYPE_SYS_MAX            = 0x0FFF,

    // ======================================
    // 模块通用事件 (0x1000 ~ 0x1FFF)
    // ======================================
    EVENT_TYPE_MOD_BASE           = 0x1000,
    EVENT_TYPE_MOD_INIT_DONE,      // 模块初始化完成
    EVENT_TYPE_MOD_START_DONE,     // 模块启动完成
    EVENT_TYPE_MOD_PAUSED,         // 模块已暂停
    EVENT_TYPE_MOD_RESUMED,        // 模块已恢复
    EVENT_TYPE_MOD_STOPPED,        // 模块已停止
    EVENT_TYPE_MOD_FAULT,          // 模块故障
    EVENT_TYPE_MOD_MAX            = 0x1FFF,

    // ======================================
    // 视频采集服务事件 (0x2000 ~ 0x2FFF)
    // ======================================
    EVENT_TYPE_CAPTURE_BASE        = 0x2000,
    EVENT_TYPE_CAPTURE_READY,      // 采集服务就绪
    EVENT_TYPE_CAPTURE_RUNNING,    // 采集服务运行中
    EVENT_TYPE_CAPTURE_PROTO_READY,// 视频帧已发布
    EVENT_TYPE_CAPTURE_STOPPED,    // 采集服务停止
    EVENT_TYPE_CAPTURE_ERROR,      // 采集服务故障
    EVENT_TYPE_CAPTURE_MAX         = 0x2FFF,

    // ======================================
    // 人脸检测服务事件 (0x3000 ~ 0x3FFF)
    // ======================================
    EVENT_TYPE_FACE_BASE           = 0x3000,
    EVENT_TYPE_FACE_READY,         // 人脸检测就绪
    EVENT_TYPE_FACE_RUNNING,       // 人脸检测运行中
    EVENT_TYPE_FACE_PROCESS_START, // 开始处理视频帧
    EVENT_TYPE_FACE_PROCESS_DONE,  // 人脸检测完成
    EVENT_TYPE_FACE_STOPPED,       // 人脸检测停止
    EVENT_TYPE_FACE_ERROR,         // 人脸检测故障
    EVENT_TYPE_FACE_MAX            = 0x3FFF,

    // ======================================
    // 人脸检测服务事件 (0x4000 ~ 0x4FFF)
    // ======================================
    EVENT_TYPE_NET_BASE           = 0x4000,
    EVENT_TYPE_NET_READY,         // 就绪
    EVENT_TYPE_NET_PROCESS_START, // 开始处理
    EVENT_TYPE_NET_PROCESS_DONE,  // 完成
    EVENT_TYPE_NET_STOPPED,       // 停止
    EVENT_TYPE_NET_ERROR,         // 故障
    EVENT_TYPE_NET_MAX            = 0x4FFF,
    // ======================================
    // Demo应用事件 (0x6000 ~ 0x6FFF)
    // ======================================
    EVENT_TYPE_DEMO_BASE           = 0x6000,
    EVENT_TYPE_DEMO_RUNNING,       // Demo运行中
    EVENT_TYPE_DEMO_EXIT,          // Demo退出
    EVENT_TYPE_DEMO_MAX            = 0x6FFF
} event_type_t;

// ==========================================================================
// 二、全局数据类型枚举（✅ 核心修改：100%匹配两个服务的DataBus调用）
// 采集服务：DATA_TYPE_VIDEO (YUYV原始帧)
// 人脸服务：DATA_TYPE_VIDEO_RGB (AI处理RGB帧) + DATA_TYPE_AI_RESULT (检测结果)
// ==========================================================================
typedef enum {
    DATA_TYPE_INVALID = 0,         // 无效类型

    // 视频数据
    DATA_TYPE_VIDEO = 0x01,  // 摄像头原始帧（采集服务发布）
    DATA_TYPE_VIDEO_YUV420,  // YUV格式帧
    DATA_TYPE_VIDEO_RGB,     // AI处理后RGB帧（人脸服务发布）

    // AI数据
    DATA_TYPE_AI_RESULT = 0x10,    // 人脸检测结果数据

    // 音频数据（未使用）
    DATA_TYPE_AUDIO_FRAME = 0x20,
    DATA_TYPE_AUDIO_PCM,

    DATA_TYPE_MAX = 0xFF
} data_type_t;

// ==========================================================================
// 三、系统全局配置（完整保留，无修改）
// ==========================================================================
#define CONFIG_APP_NAME "Vision AI Demo"
// ==================================================================================
// 运行模式配置
// 0 = 调试模式（前台运行，支持键盘S控制）
// 1 = 产品模式（守护进程，后台自动运行，无需键盘）
// ==================================================================================
#define RUN_PRODUCT_MODE  0

// 双总线配置
#define CONFIG_EVENT_BUS_MAX_SUBSCRIBERS 64
#define CONFIG_EVENT_BUS_MAX_QUEUE 256
#define CONFIG_EVENT_BUS_ENABLE_STATS true

#define CONFIG_DATA_BUS_MAX_FRAMES 128
#define CONFIG_DATA_BUS_ENABLE_STATS true

// 全局FSM配置
// #define CONFIG_GLOBAL_FSM_MAX_MODULES 16 // 未使用

// 采集服务配置
#define CONFIG_CAPTURE_DEV_PATH "/dev/video1"
#define CONFIG_CAPTURE_FORMAT 0  // 0=YUYV 1=NV12 2=MJPEG
#define CONFIG_CAPTURE_FPS 30
#define CONFIG_CAPTURE_BUF_COUNT 8
#define CONFIG_CAPTURE_LOCK_EXPOSURE true
#define CONFIG_CAPTURE_LOCK_WHITE_BALANCE true
#define CONFIG_CAPTURE_LOCK_GAIN true


// AI模型配置 原来320x240
#define CONFIG_AI_MODEL_PATH "./RFB-320-quant-KL-5792.mnn"
#define CONFIG_AI_INPUT_W    160
#define CONFIG_AI_INPUT_H    120
#define CONFIG_AI_SCORE_THRESH 0.65f
#define CONFIG_AI_IOU_THRESH   0.3f

// 人脸检测最大数量
#define MAX_FACES             100


// ==========================================================================
// 【全局统一视频基准宏】唯一入口，所有模块共用，禁止单独修改！
// ==========================================================================
#define GLOBAL_VIDEO_FPS                15           // 全局统一帧率（采集=推流=RTSP）
#define GLOBAL_VIDEO_WIDTH              640         // 统一分辨率宽
#define GLOBAL_VIDEO_HEIGHT             360         // 统一分辨率高
#define GLOBAL_JPEG_QUALITY             75          // 统一JPEG质量

// 自动计算帧间隔（推流用：毫秒）
#define GLOBAL_FRAME_INTERVAL_MS        (1000 / GLOBAL_VIDEO_FPS)
// 自动计算帧间隔（RTSP底层用：微秒）
#define GLOBAL_FRAME_INTERVAL_US        (1000000 / GLOBAL_VIDEO_FPS)


#ifdef __cplusplus
}
#endif

#endif /* VISION_AI_CONFIG_H */