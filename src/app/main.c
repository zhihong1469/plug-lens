// src/app/main.c
#include "vision_ai_config.h"
#include "event_bus.h"
#include "data_bus.h"
#include "global_fsm.h"
#include "plugin_loader.h"
#include "demo_app.h"
#include "capture_srv.h"  // 【新增】包含采集服务头文件
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>  // 【新增】用于设置非阻塞IO

// ==========================================================================
// 全局句柄定义
// ==========================================================================
event_bus_handle_t g_event_bus = NULL;
data_bus_handle_t g_data_bus = NULL;
global_fsm_handle_t g_global_fsm = NULL;
static volatile bool g_running = true;

// ==========================================================================
// 【新增】采集服务插件描述符（临时放在这里，后续应移到plugins目录）
// ==========================================================================
#include "capture_srv.h"
static capture_srv_handle_t g_capture_srv_handle = NULL;

static int capture_srv_plugin_init(void)
{
    LOG_I("Capture Srv Plugin: Initializing...");
    capture_srv_config_t cfg = {
        .link_config = {
            .hal_config = {
                .dev_path = CONFIG_CAPTURE_DEV_PATH,
                .width = CONFIG_CAPTURE_WIDTH,
                .height = CONFIG_CAPTURE_HEIGHT,
                .format = CONFIG_CAPTURE_FORMAT,
                .fps = CONFIG_CAPTURE_FPS,
                .buf_count = CONFIG_CAPTURE_BUF_COUNT,
                .lock_exposure = CONFIG_CAPTURE_LOCK_EXPOSURE,
                .lock_white_balance = CONFIG_CAPTURE_LOCK_WHITE_BALANCE,
                .lock_gain = CONFIG_CAPTURE_LOCK_GAIN,
            },
            .frame_pool_size = CONFIG_FRAME_LINK_POOL_SIZE,
            .queue_size = CONFIG_FRAME_LINK_QUEUE_SIZE,
        },
        .auto_start = false, // 不自动启动，等待命令
    };
    
    int ret = capture_srv_init(&cfg, &g_capture_srv_handle);
    if (ret != 0) {
        LOG_E("Capture Srv Plugin: Failed to init");
        return -1;
    }
    LOG_I("Capture Srv Plugin: Initialized");
    return 0;
}

static int capture_srv_plugin_start(void)
{
    LOG_I("Capture Srv Plugin: Starting...");
    // 这里不自动启动，等待用户命令 's' 启动
    LOG_I("Capture Srv Plugin: Ready (press 's' to start capture)");
    return 0;
}

static int capture_srv_plugin_stop(void)
{
    LOG_I("Capture Srv Plugin: Stopping...");
    capture_srv_stop(g_capture_srv_handle);
    return 0;
}

static int capture_srv_plugin_deinit(void)
{
    LOG_I("Capture Srv Plugin: Deinitializing...");
    capture_srv_deinit(g_capture_srv_handle);
    return 0;
}

static const plugin_desc_t g_capture_srv_plugin_desc = {
    .name = "capture_srv",
    .init = capture_srv_plugin_init,
    .start = capture_srv_plugin_start,
    .stop = capture_srv_plugin_stop,
    .deinit = capture_srv_plugin_deinit,
};

// ==========================================================================
// 信号处理函数
// ==========================================================================
static void _main_signal_handler(int sig)
{
    (void)sig;
    LOG_I("Main: Received signal %d, stopping...", sig);
    g_running = false;
}

// ==========================================================================
// 【新增】导出给demo_app调用的函数
// ==========================================================================
void demo_app_trigger_capture_start(void)
{
    if (g_capture_srv_handle != NULL) {
        capture_srv_start(g_capture_srv_handle);
    }
}

void demo_app_trigger_capture_stop(void)
{
    if (g_capture_srv_handle != NULL) {
        capture_srv_stop(g_capture_srv_handle);
    }
}

// ==========================================================================
// 主函数
// ==========================================================================
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    int ret = 0;

    // 【新增】设置标准输入为非阻塞模式
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    LOG_I("========================================");
    LOG_I("  %s", CONFIG_APP_NAME);
    LOG_I("========================================");

    // 1. 订阅信号
    signal(SIGINT, _main_signal_handler);
    signal(SIGTERM, _main_signal_handler);

    // 2. 初始化核心框架
    LOG_I("Main: Initializing framework...");
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

    data_bus_config_t data_bus_cfg = {
        .max_frames = CONFIG_DATA_BUS_MAX_FRAMES,
        .enable_stats = CONFIG_DATA_BUS_ENABLE_STATS,
    };
    ret = data_bus_init(&data_bus_cfg, &g_data_bus);
    if (ret != 0) {
        LOG_E("Main: Failed to initialize Data Bus");
        goto error_event_bus;
    }

    global_fsm_config_t global_fsm_cfg = {
        .max_module_count = CONFIG_GLOBAL_FSM_MAX_MODULES,
    };
    ret = global_fsm_init(&global_fsm_cfg, &g_global_fsm);
    if (ret != 0) {
        LOG_E("Main: Failed to initialize Global FSM");
        goto error_data_bus;
    }
    LOG_I("Main: Framework initialized successfully");

    // 3. 加载插件
    LOG_I("Main: Loading plugins...");
    ret = plugin_register(&g_capture_srv_plugin_desc); // 【新增】先注册采集服务
    if (ret != 0) {
        LOG_E("Main: Failed to register capture_srv plugin");
        goto error_global_fsm;
    }
    
    ret = plugin_register(&g_demo_app_desc);
    if (ret != 0) {
        LOG_E("Main: Failed to register demo_app plugin");
        goto error_global_fsm;
    }
    LOG_I("Main: Plugins loaded successfully");

    // 4. 初始化所有插件
    ret = plugin_init_all();
    if (ret != 0) {
        LOG_E("Main: Failed to initialize plugins");
        goto error_plugins;
    }

    // 5. 启动所有插件
    LOG_I("Main: Starting plugins...");
    ret = plugin_start_all();
    if (ret != 0) {
        LOG_E("Main: Failed to start plugins");
        goto error_plugins_stop;
    }

    LOG_I("Main: System is running...");

    // 6. 【修改】主循环：调用命令行处理
    extern void demo_app_command_loop(void); // 声明demo_app的命令循环
    while (g_running) {
        demo_app_command_loop(); // 【关键】调用命令处理
        
        // 尝试从采集服务获取一帧并打印日志（简化测试）
        if (g_capture_srv_handle != NULL) {
            video_frame_t frame;
            ret = capture_srv_get_frame(g_capture_srv_handle, &frame, 10); // 10ms超时
            if (ret == 0) {
                LOG_I("Main: Got frame! index=%u, len=%u, ts=%llu", 
                      frame.index, frame.length, (unsigned long long)frame.timestamp);
                capture_srv_put_frame(g_capture_srv_handle, &frame);
            }
        }
        
        usleep(10000); // 10ms循环，不要占用太多CPU
    }

    // 7. 正常退出流程
    LOG_I("Main: Stopping plugins...");
    plugin_stop_all();
    plugin_deinit_all();
    global_fsm_deinit(g_global_fsm);
    data_bus_deinit(g_data_bus);
    event_bus_deinit(g_event_bus);
    LOG_I("Main: System exited normally");
    return 0;

    // 错误退出流程
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