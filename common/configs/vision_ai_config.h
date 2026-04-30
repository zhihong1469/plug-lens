// common/configs/vision_ai_config.h（解耦）
#ifndef VISION_AI_CONFIG_H
#define VISION_AI_CONFIG_H

// 【核心】只包含标准C库头文件，不包含任何业务层头文件！
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【配置集中管理】所有系统参数统一在这里定义
// ==========================================================================

// 通用配置
#define CONFIG_APP_NAME "Vision AI Demo"
#define CONFIG_AUTO_START true

// 双总线配置（纯数值，不依赖event_bus.h/data_bus.h类型）
#define CONFIG_EVENT_BUS_MAX_SUBSCRIBERS 64
#define CONFIG_EVENT_BUS_MAX_QUEUE 256
#define CONFIG_EVENT_BUS_ENABLE_STATS true

#define CONFIG_DATA_BUS_MAX_FRAMES 128
#define CONFIG_DATA_BUS_ENABLE_STATS true

// 全局FSM配置（纯数值）
#define CONFIG_GLOBAL_FSM_MAX_MODULES 16

// 采集服务配置（纯数值，不依赖video_hal.h类型）
#define CONFIG_CAPTURE_DEV_PATH "/dev/video0"
#define CONFIG_CAPTURE_WIDTH 640
#define CONFIG_CAPTURE_HEIGHT 480
#define CONFIG_CAPTURE_FORMAT 0  // 0=VIDEO_PIX_FMT_YUYV，纯数值
#define CONFIG_CAPTURE_FPS 30
#define CONFIG_CAPTURE_BUF_COUNT 4
#define CONFIG_CAPTURE_LOCK_EXPOSURE true
#define CONFIG_CAPTURE_LOCK_WHITE_BALANCE true
#define CONFIG_CAPTURE_LOCK_GAIN true

// 帧链路配置（纯数值）
#define CONFIG_FRAME_LINK_POOL_SIZE 8
#define CONFIG_FRAME_LINK_QUEUE_SIZE 4

#endif /* VISION_AI_CONFIG_H */