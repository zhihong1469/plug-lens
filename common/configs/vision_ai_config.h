/**
 * @file    vision_ai_config.h
 * @brief   Global configuration and protocol definition for Vision AI system
 *          视觉AI系统全局配置与通信协议定义
 * @details 核心功能说明：
 *          1. 定义全系统统一的DataBus总线名称，服务间硬编码禁止
 *          2. 规划全局事件类型编码区间（系统/模块/视频/人脸/推流/Demo）
 *          3. 统一数据类型枚举，适配视频采集、AI推理、流传输全链路
 *          4. 集成采集服务、AI模型、视频参数、双总线核心配置
 *          5. 基于纯DataBus架构，对接采集服务+人脸检测服务+推流服务
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    全局使用规范：
 *          1. 总线名称、事件类型、数据类型全系统唯一，禁止修改
 *          2. 视频基准宏为全局唯一入口，所有模块共用，禁止单独调整
 *          3. 事件编码按区间划分，新模块需遵循规划扩展
 */
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
/** 系统事件总线名称 | 系统级控制事件通信总线 */
#define SYS_EVENT_BUS_NAME        "sys_event"
/** 系统数据总线名称 | 通用数据传输总线 */
#define SYS_DATA_BUS_NAME         "sys_data"
/** 视频数据总线名称 | 摄像头YUYV原始帧总线（采集服务生产） */
#define VIDEO_DATA_BUS_NAME       "video"
/** AI RGB数据总线名称 | AI模型输入RGB帧总线（人脸服务生产） */
#define AI_RGB_DATA_BUS_NAME      "ai_rgb"
/** 人脸结果数据总线名称 | 人脸检测结果输出总线 */
#define FACE_YUV_DATA_BUS_NAME    "face_result"
/** H264流数据总线名称 | RTSP推流H264码流传输总线 */
#define H264_RTSP_DATA_BUS_NAME   "h264_stream_bus"

// ==========================================================================
// 【全局通用事件类型】
// 所有服务都需要订阅的系统级控制事件，仅此一处定义
// 区间规划：
// 0x0000 ~ 0x0FFF：系统全局事件
// 0x1000 ~ 0x1FFF：模块通用基础事件
// 0x2000 ~ 0x2FFF：视频采集服务专属事件
// 0x3000 ~ 0x3FFF：人脸检测服务专属事件
// 0x4000 ~ 0x4FFF：推流服务专属事件
// 0x6000 ~ 0x6FFF：Demo应用服务专属事件
// ==========================================================================
/**
 * @brief   全局系统事件类型枚举
 * @details 全系统统一事件编码，基于DataBus实现服务间解耦通信
 *          按功能模块划分编码区间，便于扩展与维护
 */
typedef enum {
    // ======================================
    // 系统全局事件 (0x0000 ~ 0x0FFF)
    // ======================================
    EVENT_TYPE_INVALID            = 0x0000,    /**< 无效事件 | 默认初始值 */
    EVENT_TYPE_SYS_BASE           = 0x0001,    /**< 系统事件基址 | 区间起始标记 */
    EVENT_TYPE_SYS_CORE_READY,                 /**< 系统核心就绪 | 双总线初始化完成 */
    EVENT_TYPE_SYS_PAUSE,                      /**< 系统暂停 | 全局暂停所有模块 */
    EVENT_TYPE_SYS_RESUME,                     /**< 系统恢复 | 全局恢复所有模块 */
    EVENT_TYPE_SYS_STOP,                       /**< 系统停止 | 优雅停止所有服务 */
    EVENT_TYPE_SYS_SHUTDOWN,                   /**< 系统关机 | 设备关机指令 */
    EVENT_TYPE_SYS_ERROR,                      /**< 系统错误 | 全局异常通知 */
    EVENT_TYPE_SYS_MAX            = 0x0FFF,    /**< 系统事件上限 | 区间结束标记 */

    // ======================================
    // 模块通用事件 (0x1000 ~ 0x1FFF)
    // ======================================
    EVENT_TYPE_MOD_BASE           = 0x1000,    /**< 模块事件基址 | 区间起始标记 */
    EVENT_TYPE_MOD_INIT_DONE,                  /**< 模块初始化完成 | 模块初始化成功通知 */
    EVENT_TYPE_MOD_START_DONE,                 /**< 模块启动完成 | 模块进入运行状态 */
    EVENT_TYPE_MOD_PAUSED,                     /**< 模块已暂停 | 模块暂停状态通知 */
    EVENT_TYPE_MOD_RESUMED,                    /**< 模块已恢复 | 模块恢复运行通知 */
    EVENT_TYPE_MOD_STOPPED,                    /**< 模块已停止 | 模块停止完成通知 */
    EVENT_TYPE_MOD_FAULT,                      /**< 模块故障 | 模块异常错误通知 */
    EVENT_TYPE_MOD_MAX            = 0x1FFF,    /**< 模块事件上限 | 区间结束标记 */

    // ======================================
    // 视频采集服务事件 (0x2000 ~ 0x2FFF)
    // ======================================
    EVENT_TYPE_CAPTURE_BASE        = 0x2000,   /**< 采集服务事件基址 | 区间起始标记 */
    EVENT_TYPE_CAPTURE_READY,                  /**< 采集服务就绪 | 摄像头初始化完成 */
    EVENT_TYPE_CAPTURE_RUNNING,                /**< 采集服务运行中 | 正常采集视频帧 */
    EVENT_TYPE_CAPTURE_PROTO_READY,            /**< 视频帧已发布 | 帧数据推送至总线 */
    EVENT_TYPE_CAPTURE_STOPPED,                /**< 采集服务停止 | 采集任务结束 */
    EVENT_TYPE_CAPTURE_ERROR,                  /**< 采集服务故障 | 摄像头/驱动异常 */
    EVENT_TYPE_CAPTURE_MAX         = 0x2FFF,   /**< 采集服务事件上限 | 区间结束标记 */

    // ======================================
    // 人脸检测服务事件 (0x3000 ~ 0x3FFF)
    // ======================================
    EVENT_TYPE_FACE_BASE           = 0x3000,   /**< 人脸服务事件基址 | 区间起始标记 */
    EVENT_TYPE_FACE_READY,                     /**< 人脸检测就绪 | 模型加载完成 */
    EVENT_TYPE_FACE_RUNNING,                   /**< 人脸检测运行中 | 正常处理视频帧 */
    EVENT_TYPE_FACE_PROCESS_START,             /**< 开始处理视频帧 | AI推理启动 */
    EVENT_TYPE_FACE_PROCESS_DONE,              /**< 人脸检测完成 | 结果推送至总线 */
    EVENT_TYPE_FACE_STOPPED,                   /**< 人脸检测停止 | 处理任务结束 */
    EVENT_TYPE_FACE_ERROR,                     /**< 人脸检测故障 | 模型/推理异常 */
    EVENT_TYPE_FACE_MAX            = 0x3FFF,   /**< 人脸服务事件上限 | 区间结束标记 */

    // ======================================
    // 推流服务事件 (0x4000 ~ 0x4FFF)
    // ======================================
    EVENT_TYPE_NET_BASE           = 0x4000,    /**< 推流服务事件基址 | 区间起始标记 */
    EVENT_TYPE_NET_READY,                       /**< 推流服务就绪 | 网络初始化完成 */
    EVENT_TYPE_NET_PROCESS_START,               /**< 推流开始 | 开始编码传输数据 */
    EVENT_TYPE_RTSP_CONNECTED,                  /**< RTSP客户端已连接 | 客户端接入通知 */
    EVENT_TYPE_RTSP_DISCONNECTED,               /**< RTSP客户端已断开 | 客户端断开通知 */
    EVENT_TYPE_NET_PROCESS_DONE,                /**< 推流完成 | 单次帧处理结束 */
    EVENT_TYPE_NET_STOPPED,                     /**< 推流服务停止 | 推流任务结束 */
    EVENT_TYPE_NET_ERROR,                       /**< 推流服务故障 | 网络/编码异常 */
    EVENT_TYPE_NET_MAX            = 0x4FFF,     /**< 推流服务事件上限 | 区间结束标记 */

    // ======================================
    // Demo应用事件 (0x6000 ~ 0x6FFF)
    // ======================================
    EVENT_TYPE_DEMO_BASE           = 0x6000,   /**< Demo应用事件基址 | 区间起始标记 */
    EVENT_TYPE_DEMO_RUNNING,                    /**< Demo运行中 | 应用正常运行 */
    EVENT_TYPE_DEMO_EXIT,                       /**< Demo退出 | 应用程序关闭 */
    EVENT_TYPE_DEMO_MAX            = 0x6FFF    /**< Demo应用事件上限 | 区间结束标记 */
} event_type_t;

// ==========================================================================
// 二、全局数据类型枚举
// 采集服务：DATA_TYPE_VIDEO (YUYV原始帧)
// 人脸服务：DATA_TYPE_VIDEO_RGB (AI处理RGB帧) + DATA_TYPE_AI_RESULT (检测结果)
// ==========================================================================
/**
 * @brief   全局数据类型枚举
 * @details DataBus传输数据类型定义，全系统统一数据格式标准
 *          覆盖视频、AI、音频全场景数据传输
 */
typedef enum {
    DATA_TYPE_INVALID = 0,                     /**< 无效数据类型 | 默认初始值 */

    // 视频数据类型
    DATA_TYPE_VIDEO = 0x01,                    /**< 摄像头原始帧 | 采集服务发布YUYV格式 */
    DATA_TYPE_VIDEO_YUV420,                    /**< YUV420格式视频帧 | 通用压缩视频格式 */
    DATA_TYPE_VIDEO_RGB,                       /**< RGB格式视频帧 | AI模型输入专用格式 */
    DATA_TYPE_H264,                            /**< H264编码码流 | RTSP推流专用格式 */

    // AI数据类型
    DATA_TYPE_AI_RESULT = 0x10,                /**< AI检测结果数据 | 人脸检测坐标/置信度 */

    // 音频数据类型（预留扩展）
    DATA_TYPE_AUDIO_FRAME = 0x20,              /**< 音频原始帧 | 预留接口 */
    DATA_TYPE_AUDIO_PCM,                       /**< PCM音频数据 | 预留接口 */

    DATA_TYPE_MAX = 0xFF                       /**< 数据类型上限 | 区间结束标记 */
} data_type_t;

// ==========================================================================
// 三、系统全局配置
// ==========================================================================
/** 应用程序名称 | Vision AI演示程序标识 */
#define CONFIG_APP_NAME "Vision AI Demo"

/**
 * @brief   系统运行模式配置
 * @details 0=调试模式(前台运行，键盘控制)；1=产品模式(后台守护进程)
 */
// 脚本守护和程序内部守护只能选其一
#define USE_SH 1
#define RUN_PRODUCT_MODE  1

// --------------------- 双总线(Event/DataBus)核心配置 ---------------------
/** 事件总线最大订阅者数 | 支持多服务并发订阅 */
#define CONFIG_EVENT_BUS_MAX_SUBSCRIBERS 64
/** 事件总线最大队列长度 | 事件缓存容量 */
#define CONFIG_EVENT_BUS_MAX_QUEUE 256
/** 事件总线统计功能开关 | 启用运行状态统计 */
#define CONFIG_EVENT_BUS_ENABLE_STATS true

/** 数据总线最大帧缓存数 | 视频帧缓存容量 */
#define CONFIG_DATA_BUS_MAX_FRAMES 128
/** 数据总线统计功能开关 | 启用运行状态统计 */
#define CONFIG_DATA_BUS_ENABLE_STATS true

// --------------------- 视频采集服务配置 ---------------------
/** 摄像头设备节点 | 嵌入式Linux V4L2设备路径 */
#define CONFIG_CAPTURE_DEV_PATH "/dev/video18"
/** 视频采集格式 | 0=YUYV 1=NV12 2=MJPEG */
#define CONFIG_CAPTURE_FORMAT 0
/** 视频采集帧率 | 单位：FPS */
#define CONFIG_CAPTURE_FPS 30
/** 采集缓冲区数量 | V4L2内核缓冲帧数 */
#define CONFIG_CAPTURE_BUF_COUNT 8
/** 曝光锁定开关 | 工业视觉固定曝光，提升AI稳定性 */
#define CONFIG_CAPTURE_LOCK_EXPOSURE true
/** 白平衡锁定开关 | 固定色温，避免画面色彩漂移 */
#define CONFIG_CAPTURE_LOCK_WHITE_BALANCE true
/** 增益锁定开关 | 固定模拟增益，提升检测稳定性 */
#define CONFIG_CAPTURE_LOCK_GAIN true

// --------------------- AI人脸检测模型配置 ---------------------
/** AI模型文件路径 | MNN量化模型存储路径 */
#define CONFIG_AI_MODEL_PATH "./RFB-320-quant-KL-5792.mnn"
/** AI模型输入宽度 | 模型推理图像宽度 */
#define CONFIG_AI_INPUT_W    320
/** AI模型输入高度 | 模型推理图像高度 */
#define CONFIG_AI_INPUT_H    240
/** AI置信度阈值 | 人脸检测最低置信度(0~1) */
#define CONFIG_AI_SCORE_THRESH 0.25f
/** AI非极大值抑制IOU阈值 | 去重重叠检测框 */
#define CONFIG_AI_IOU_THRESH   0.3f

/** 单帧最大人脸检测数量 | 限制最大检测目标数 */
#define MAX_FACES             10

// ==========================================================================
// 【全局统一视频基准宏】唯一入口，所有模块共用，禁止单独修改！
// ==========================================================================
/** 全局统一视频帧率 | 采集/推流/RTSP全链路统一 */
#define GLOBAL_VIDEO_FPS                14
/** 全局统一视频宽度 | 全系统标准分辨率宽度 */
#define GLOBAL_VIDEO_WIDTH              640
/** 全局统一视频高度 | 全系统标准分辨率高度 */
#define GLOBAL_VIDEO_HEIGHT             360
/** 全局统一JPEG压缩质量 | 0~100，越高画质越好 */
#define GLOBAL_JPEG_QUALITY             75

/** 全局帧间隔(毫秒) | 推流模块定时发送用 */
#define GLOBAL_FRAME_INTERVAL_MS        (1000 / GLOBAL_VIDEO_FPS)
/** 全局帧间隔(微秒) | RTSP底层时间戳用 */
#define GLOBAL_FRAME_INTERVAL_US        (1000000 / GLOBAL_VIDEO_FPS)

#ifdef __cplusplus
}
#endif

#endif /* VISION_AI_CONFIG_H */