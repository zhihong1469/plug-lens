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
#define CONFIG_CAPTURE_HEIGHT 480
#define CONFIG_CAPTURE_FORMAT 0  // 0=VIDEO_PIX_FMT_YUYV
#define CONFIG_CAPTURE_FPS 30
#define CONFIG_CAPTURE_BUF_COUNT 4
#define CONFIG_CAPTURE_LOCK_EXPOSURE true
#define CONFIG_CAPTURE_LOCK_WHITE_BALANCE true
#define CONFIG_CAPTURE_LOCK_GAIN true

// 帧链路配置
#define CONFIG_FRAME_LINK_POOL_SIZE 8
#define CONFIG_FRAME_LINK_QUEUE_SIZE 4

// ==========================================================================
// 【修改】移除对bus/fsm头文件的依赖，避免循环依赖
// 改为在main.c中定义全局句柄，这里只做声明
// ==========================================================================
typedef void* event_bus_handle_t;
typedef void* data_bus_handle_t;
typedef void* global_fsm_handle_t;

extern event_bus_handle_t g_event_bus;
extern data_bus_handle_t g_data_bus;
extern global_fsm_handle_t g_global_fsm;

#endif /* VISION_AI_CONFIG_H */