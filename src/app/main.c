// src/app/main.c
#include "vision_ai_config.h"
#include "event_bus.h"
#include "data_bus.h"
#include "global_fsm.h"
#include "plugin_loader.h"
#include "demo_app.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// ==========================================================================
// [新增] 全局句柄定义（框架初始化后赋值）
// ==========================================================================
event_bus_handle_t g_event_bus = NULL;
data_bus_handle_t g_data_bus = NULL;
global_fsm_handle_t g_global_fsm = NULL;
static volatile bool g_running = true;

// ==========================================================================
// [新增] 信号处理函数
// ==========================================================================
static void _main_signal_handler(int sig)
{
    (void)sig;
    LOG_I("Main: Received signal %d, stopping...", sig);
    g_running = false;
}

// ==========================================================================
// 主函数
// ==========================================================================
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    int ret = 0;

    LOG_I("========================================");
    LOG_I("  %s", CONFIG_APP_NAME);
    LOG_I("========================================");

    // ======================================================================
    // 1. 订阅信号
    // ======================================================================
    signal(SIGINT, _main_signal_handler);
    signal(SIGTERM, _main_signal_handler);

    // ======================================================================
    // 2. [新增] 初始化核心框架（双总线、全局FSM）
    // ======================================================================
    LOG_I("Main: Initializing framework...");

    // 初始化Event Bus
    event_bus_config_t event_bus_cfg = {
        .max_subscribers = CONFIG_EVENT_BUS_MAX_SUBSCRIBERS,
        .max_event_queue = CONFIG_EVENT_BUS_MAX_QUEUE,
        .enable_stats = CONFIG_EVENT_BUS_ENABLE_STATS,
    };
    ret = event_bus_init(&event_bus_cfg, &g_event_bus);
    if (ret != 0) {
        LOG_E("Main: Failed to initialize Event Bus");
        goto error_exit;
    }

    // 初始化Data Bus
    data_bus_config_t data_bus_cfg = {
        .max_frames = CONFIG_DATA_BUS_MAX_FRAMES,
        .enable_stats = CONFIG_DATA_BUS_ENABLE_STATS,
    };
    ret = data_bus_init(&data_bus_cfg, &g_data_bus);
    if (ret != 0) {
        LOG_E("Main: Failed to initialize Data Bus");
        goto error_event_bus;
    }

    // 初始化全局FSM
    global_fsm_config_t global_fsm_cfg = {
        .max_module_count = CONFIG_GLOBAL_FSM_MAX_MODULES,
    };
    ret = global_fsm_init(&global_fsm_cfg, &g_global_fsm);
    if (ret != 0) {
        LOG_E("Main: Failed to initialize Global FSM");
        goto error_data_bus;
    }

    LOG_I("Main: Framework initialized successfully");

    // ======================================================================
    // 3. 加载插件（静态插件注册）
    // ======================================================================
    LOG_I("Main: Loading plugins...");

    // 注册demo_app插件
    ret = plugin_register(&g_demo_app_desc);
    if (ret != 0) {
        LOG_E("Main: Failed to register demo_app plugin");
        goto error_global_fsm;
    }

    LOG_I("Main: Plugins loaded successfully");

    // ======================================================================
    // 4. 初始化所有插件
    // ======================================================================
    ret = plugin_init_all();
    if (ret != 0) {
        LOG_E("Main: Failed to initialize plugins");
        goto error_plugins;
    }

    // ======================================================================
    // 5. 启动所有插件
    // ======================================================================
    LOG_I("Main: Starting plugins...");
    ret = plugin_start_all();
    if (ret != 0) {
        LOG_E("Main: Failed to start plugins");
        goto error_plugins_stop;
    }

    LOG_I("Main: System is running...");

    // ======================================================================
    // 6. [新增] 主循环（不阻塞，让出CPU）
    // ======================================================================
    while (g_running) {
        sleep(1); // 让出CPU
    }

    // ======================================================================
    // 7. 正常退出流程
    // ======================================================================
    LOG_I("Main: Stopping plugins...");
    plugin_stop_all();
    plugin_deinit_all();
    global_fsm_deinit(g_global_fsm);
    data_bus_deinit(g_data_bus);
    event_bus_deinit(g_event_bus);
    LOG_I("Main: System exited normally");
    return 0;

    // ======================================================================
    // 错误退出流程
    // ======================================================================
error_plugins_stop:
    plugin_stop_all();
error_plugins:
    plugin_deinit_all();
error_global_fsm:
    global_fsm_deinit(g_global_fsm);
error_data_bus:
    data_bus_deinit(g_data_bus);
error_event_bus:
    event_bus_deinit(g_event_bus);
error_exit:
    LOG_E("Main: System exited with error");
    return -1;
}