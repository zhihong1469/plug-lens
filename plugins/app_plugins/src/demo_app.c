// plugins/app_plugins/src/demo_app.c（配置翻译示例）
#include "demo_app.h"
#include "vision_ai_config.h"
#include "event_bus.h"      // 业务层自己包含业务头文件
#include "data_bus.h"
#include "global_fsm.h"
#include "video_hal.h"
#include "frame_link.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

// ==========================================================================
// 内部全局变量（仅插件内部使用，不暴露）
// ==========================================================================
static event_bus_handle_t g_event_bus = NULL;
static data_bus_handle_t g_data_bus = NULL;
static global_fsm_handle_t g_global_fsm = NULL;
static volatile bool g_running = false;

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static void _demo_app_signal_handler(int sig);
static void _demo_app_global_state_change_cb(global_state_t old_state, global_state_t new_state, void *user_data);
static void _demo_app_event_error_cb(const event_t *event, void *user_data);
static void _demo_app_event_capture_frame_ready_cb(const event_t *event, void *user_data);
static void _demo_app_print_help(void);
static void _demo_app_command_loop(void);

static void _demo_app_translate_config(video_config_t *hal_cfg,
                                        frame_link_config_t *link_cfg,
                                        event_bus_config_t *event_bus_cfg,
                                        data_bus_config_t *data_bus_cfg,
                                        global_fsm_config_t *fsm_cfg)
{
    // HAL配置
    hal_cfg->dev_path = CONFIG_CAPTURE_DEV_PATH;
    hal_cfg->width = CONFIG_CAPTURE_WIDTH;
    hal_cfg->height = CONFIG_CAPTURE_HEIGHT;
    hal_cfg->format = (video_format_t)CONFIG_CAPTURE_FORMAT; // 翻译
    hal_cfg->fps = CONFIG_CAPTURE_FPS;
    hal_cfg->buf_count = CONFIG_CAPTURE_BUF_COUNT;
    hal_cfg->lock_exposure = CONFIG_CAPTURE_LOCK_EXPOSURE;
    hal_cfg->lock_white_balance = CONFIG_CAPTURE_LOCK_WHITE_BALANCE;
    hal_cfg->lock_gain = CONFIG_CAPTURE_LOCK_GAIN;

    // Link配置
    link_cfg->hal_config = *hal_cfg;
    link_cfg->frame_pool_size = CONFIG_FRAME_LINK_POOL_SIZE;
    link_cfg->queue_size = CONFIG_FRAME_LINK_QUEUE_SIZE;

    // Event Bus配置
    event_bus_cfg->max_subscribers = CONFIG_EVENT_BUS_MAX_SUBSCRIBERS;
    event_bus_cfg->max_event_queue = CONFIG_EVENT_BUS_MAX_QUEUE;
    event_bus_cfg->enable_stats = CONFIG_EVENT_BUS_ENABLE_STATS;

    // Data Bus配置
    data_bus_cfg->max_frames = CONFIG_DATA_BUS_MAX_FRAMES;
    data_bus_cfg->enable_stats = CONFIG_DATA_BUS_ENABLE_STATS;

    // Global FSM配置
    fsm_cfg->max_module_count = CONFIG_GLOBAL_FSM_MAX_MODULES;
}

// ==========================================================================
// 插件初始化函数
// ==========================================================================
static int demo_app_init(void)
{
    LOG_I("Demo App: Initializing...");

    // 1. 订阅信号（用户交互隔离）
    signal(SIGINT, _demo_app_signal_handler);
    signal(SIGTERM, _demo_app_signal_handler);

    // 2. 获取框架句柄（通过全局变量，或者框架提供的getter，这里简化为全局变量，框架层初始化时设置）
    // 注意：实际项目中框架层应该提供getter函数，避免全局变量，但这里为了简化演示
    // 假设框架层初始化时设置了g_event_bus、g_data_bus、g_global_fsm

    // 3. 订阅全局FSM状态变更
    if (g_global_fsm != NULL) {
        global_fsm_set_state_cb(g_global_fsm, _demo_app_global_state_change_cb, NULL);
    }

    // 4. 订阅总线事件
    if (g_event_bus != NULL) {
        event_bus_subscribe(g_event_bus, EVENT_TYPE_ERROR, _demo_app_event_error_cb, NULL);
        event_bus_subscribe(g_event_bus, EVENT_TYPE_CAPTURE_FRAME_READY, _demo_app_event_capture_frame_ready_cb, NULL);
    }

    LOG_I("Demo App: Initialized successfully");
    return 0;
}

// ==========================================================================
// 插件启动函数
// ==========================================================================
static int demo_app_start(void)
{
    LOG_I("Demo App: Starting...");

    g_running = true;

    // 1. 打印帮助信息
    _demo_app_print_help();

    // 2. 发布系统初始化完成事件（触发FSM状态流转）
    event_t event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_TYPE_SYSTEM_INIT;
    event.priority = EVENT_PRIORITY_HIGH;
    event.source = "demo_app";
    event_bus_publish(g_event_bus, &event);

    // 3. 启动命令循环（用户交互隔离）
    _demo_app_command_loop();

    LOG_I("Demo App: Started successfully");
    return 0;
}

// ==========================================================================
// 插件停止函数
// ==========================================================================
static int demo_app_stop(void)
{
    LOG_I("Demo App: Stopping...");

    g_running = false;

    // 1. 发布系统停止事件
    event_t event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_TYPE_SYSTEM_STOP;
    event.priority = EVENT_PRIORITY_HIGH;
    event.source = "demo_app";
    event_bus_publish(g_event_bus, &event);

    LOG_I("Demo App: Stopped successfully");
    return 0;
}

// ==========================================================================
// 插件销毁函数
// ==========================================================================
static int demo_app_deinit(void)
{
    LOG_I("Demo App: Deinitializing...");

    // 1. 取消订阅事件
    if (g_event_bus != NULL) {
        event_bus_unsubscribe(g_event_bus, EVENT_TYPE_ERROR, _demo_app_event_error_cb);
        event_bus_unsubscribe(g_event_bus, EVENT_TYPE_CAPTURE_FRAME_READY, _demo_app_event_capture_frame_ready_cb);
    }

    LOG_I("Demo App: Deinitialized successfully");
    return 0;
}

// ==========================================================================
// 插件描述符定义
// ==========================================================================
const plugin_desc_t g_demo_app_desc = {
    .name = "demo_app",
    .init = demo_app_init,
    .start = demo_app_start,
    .stop = demo_app_stop,
    .deinit = demo_app_deinit,
};

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

static void _demo_app_signal_handler(int sig)
{
    (void)sig;
    LOG_I("Demo App: Received signal %d, stopping...", sig);
    g_running = false;
}

static void _demo_app_global_state_change_cb(global_state_t old_state, global_state_t new_state, void *user_data)
{
    (void)user_data;
    LOG_I("Demo App: Global state changed: %s -> %s",
          global_state_to_str(old_state),
          global_state_to_str(new_state));

    // 根据全局状态发布业务事件（分层状态管控：只在允许的状态下发布）
    switch (new_state) {
        case GLOBAL_STATE_READY:
            // 发布系统启动事件
            event_t event;
            memset(&event, 0, sizeof(event));
            event.type = EVENT_TYPE_SYSTEM_START;
            event.priority = EVENT_PRIORITY_HIGH;
            event.source = "demo_app";
            event_bus_publish(g_event_bus, &event);
            break;
        case GLOBAL_STATE_RUNNING:
            // 发布采集启动事件
            memset(&event, 0, sizeof(event));
            event.type = EVENT_TYPE_CAPTURE_START;
            event.priority = EVENT_PRIORITY_NORMAL;
            event.source = "demo_app";
            event_bus_publish(g_event_bus, &event);
            break;
        case GLOBAL_STATE_ERROR:
            // 发布系统停止事件
            memset(&event, 0, sizeof(event));
            event.type = EVENT_TYPE_SYSTEM_STOP;
            event.priority = EVENT_PRIORITY_HIGH;
            event.source = "demo_app";
            event_bus_publish(g_event_bus, &event);
            break;
        default:
            break;
    }
}

static void _demo_app_event_error_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    // 错误统一处理
    LOG_E("Demo App: Received error event from %s: %s",
          event->source ? event->source : "unknown",
          (char*)event->data);
}

static void _demo_app_event_capture_frame_ready_cb(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    // 这里可以从Data Bus获取帧，然后发布显示事件
    // 简化演示：只打印日志
    LOG_D("Demo App: Received capture frame ready event");
}

static void _demo_app_print_help(void)
{
    printf("\n========================================\n");
    printf("  %s\n", CONFIG_APP_NAME);
    printf("========================================\n");
    printf("  Commands:\n");
    printf("    h - Print this help\n");
    printf("    q - Quit the application\n");
    printf("========================================\n\n");
}

static void _demo_app_command_loop(void)
{
    char cmd;
    while (g_running) {
        // 非阻塞读取命令（用户交互隔离）
        cmd = getchar();
        if (cmd == EOF) {
            usleep(100000);
            continue;
        }

        switch (cmd) {
            case 'h':
            case 'H':
                _demo_app_print_help();
                break;
            case 'q':
            case 'Q':
                LOG_I("Demo App: User requested quit");
                g_running = false;
                break;
            case '\n':
            case '\r':
                // 忽略换行
                break;
            default:
                LOG_W("Demo App: Unknown command '%c'", cmd);
                _demo_app_print_help();
                break;
        }
    }
}
