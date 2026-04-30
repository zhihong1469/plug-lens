// plugins/app_plugins/src/demo_app.c
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

static volatile bool g_running = false;

// 【新增】声明main.c里的控制函数
extern void demo_app_trigger_capture_start(void);
extern void demo_app_trigger_capture_stop(void);

// 内部辅助函数声明
static void _demo_app_signal_handler(int sig);
static void _demo_app_event_error_cb(const event_t *event, void *user_data);
static void _demo_app_print_help(void);

// 插件初始化函数
static int demo_app_init(void)
{
    LOG_I("Demo App: Initializing...");
    signal(SIGINT, _demo_app_signal_handler);
    signal(SIGTERM, _demo_app_signal_handler);

    if (g_event_bus == NULL) {
        LOG_E("Demo App: Event Bus is NULL");
        return -1;
    }

    event_bus_subscribe(g_event_bus, EVENT_TYPE_ERROR, _demo_app_event_error_cb, NULL);
    LOG_I("Demo App: Initialized successfully");
    return 0;
}

// 插件启动函数
static int demo_app_start(void)
{
    LOG_I("Demo App: Starting...");
    g_running = true;
    _demo_app_print_help();
    LOG_I("Demo App: Started successfully");
    return 0;
}

// 插件停止函数
static int demo_app_stop(void)
{
    LOG_I("Demo App: Stopping...");
    g_running = false;
    LOG_I("Demo App: Stopped successfully");
    return 0;
}

// 插件销毁函数
static int demo_app_deinit(void)
{
    LOG_I("Demo App: Deinitializing...");
    event_bus_unsubscribe(g_event_bus, EVENT_TYPE_ERROR, _demo_app_event_error_cb);
    LOG_I("Demo App: Deinitialized successfully");
    return 0;
}

// 插件描述符定义
const plugin_desc_t g_demo_app_desc = {
    .name = "demo_app",
    .init = demo_app_init,
    .start = demo_app_start,
    .stop = demo_app_stop,
    .deinit = demo_app_deinit,
};

// 【修改】命令行循环改为可被main.c调用
void demo_app_command_loop(void)
{
    char cmd;
    ssize_t n = read(STDIN_FILENO, &cmd, 1); // 非阻塞读
    if (n <= 0) {
        return;
    }

    switch (cmd) {
        case 'h':
        case 'H':
            _demo_app_print_help();
            break;
        case 's':
        case 'S':
            LOG_I("Demo App: Starting capture...");
            demo_app_trigger_capture_start(); // 调用main.c的函数
            break;
        case 't':
        case 'T':
            LOG_I("Demo App: Stopping capture...");
            demo_app_trigger_capture_stop();
            break;
        case 'q':
        case 'Q':
            LOG_I("Demo App: User requested quit");
            g_running = false;
            extern volatile bool g_running; // 引用main.c的g_running
            g_running = false;
            break;
        case '\n':
        case '\r':
            break;
        default:
            LOG_W("Demo App: Unknown command '%c'", cmd);
            _demo_app_print_help();
            break;
    }
}

// 内部辅助函数实现
static void _demo_app_signal_handler(int sig)
{
    (void)sig;
    LOG_I("Demo App: Received signal %d, stopping...", sig);
    g_running = false;
}

static void _demo_app_event_error_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    LOG_E("Demo App: Received error event from %s", 
          event->source ? event->source : "unknown");
}

static void _demo_app_print_help(void)
{
    printf("\n========================================\n");
    printf("  %s\n", CONFIG_APP_NAME);
    printf("========================================\n");
    printf("  Commands:\n");
    printf("    h - Print this help\n");
    printf("    s - Start video capture\n");
    printf("    t - Stop video capture\n");
    printf("    q - Quit the application\n");
    printf("========================================\n\n");
}