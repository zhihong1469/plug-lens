// common/configs/vision_ai_config.h
#ifndef VISION_AI_CONFIG_H
#define VISION_AI_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 配置集中管理
// ==========================================================================
#define CONFIG_APP_NAME "Vision AI Demo"
#define CONFIG_AUTO_START true

// 双总线配置
#define CONFIG_EVENT_BUS_MAX_SUBSCRIBERS 64
#define CONFIG_EVENT_BUS_MAX_QUEUE 256
#define CONFIG_EVENT_BUS_ENABLE_STATS true

#define CONFIG_DATA_BUS_MAX_FRAMES 128
#define CONFIG_DATA_BUS_ENABLE_STATS true

// 全局FSM配置
#define CONFIG_GLOBAL_FSM_MAX_MODULES 16

// 采集服务配置
#define CONFIG_CAPTURE_DEV_PATH "/dev/video1"
#define CONFIG_CAPTURE_WIDTH 640
#define CONFIG_CAPTURE_HEIGHT 360
#define CONFIG_CAPTURE_FORMAT 0  // 0=VIDEO_PIX_FMT_YUYV 1=VIDEO_PIX_FMT_NV12  2=VIDEO_PIX_FMT_MJPEG
#define CONFIG_CAPTURE_FPS 4
#define CONFIG_CAPTURE_BUF_COUNT 4
#define CONFIG_CAPTURE_LOCK_EXPOSURE true
#define CONFIG_CAPTURE_LOCK_WHITE_BALANCE true
#define CONFIG_CAPTURE_LOCK_GAIN true

// 帧链路配置
#define CONFIG_FRAME_LINK_POOL_SIZE 4
#define CONFIG_FRAME_LINK_QUEUE_SIZE 2

// ==========================================================================
// 【新增】AI模型配置
// ==========================================================================
#define CONFIG_AI_MODEL_PATH "./RFB-320-quant-KL-5792.mnn"
#define CONFIG_AI_INPUT_W    320
#define CONFIG_AI_INPUT_H    240
#define CONFIG_AI_SCORE_THRESH 0.65f
#define CONFIG_AI_IOU_THRESH   0.3f


#endif /* VISION_AI_CONFIG_H */