// plugins/app_plugins/src/demo_app.c（完整修复版）
#include "demo_app.h"
#include "vision_ai_config.h"
#include "event_bus.h"
#include "data_bus.h"
#include "global_fsm.h"
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
static void _demo_app_event_error_cb(const event_t *event, void *user_data);
static void _demo_app_event_capture_frame_ready_cb(const event_t *event, void *user_data);
static void _demo_app_print_help(void);
static void _demo_app_command_loop(void);

// ==========================================================================
// 插件初始化函数
// ==========================================================================
static int demo_app_init(void)
{
    LOG_I("Demo App: Initializing...");

    // 1. 订阅信号（用户交互隔离）
    signal(SIGINT, _demo_app_signal_handler);
    signal(SIGTERM, _demo_app_signal_handler);

    // 2. 【注意】这里简化处理：假设框架层初始化时设置了全局句柄
    // 实际项目中应该通过框架提供的getter函数获取

    // 3. 订阅总线事件
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
    if (g_event_bus != NULL) {
        event_bus_publish(g_event_bus, &event);
    }

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
    if (g_event_bus != NULL) {
        event_bus_publish(g_event_bus, &event);
    }

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