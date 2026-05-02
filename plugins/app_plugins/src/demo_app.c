// plugins/app_plugins/src/demo_app.c
#include "demo_app.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ==========================================================================
// 内部状态
// ==========================================================================
typedef struct {
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;
    global_fsm_handle_t g_fsm;
    capture_srv_handle_t cap_srv;
    
    bool running;
    int cap_sub_id; // 采集事件订阅ID
} demo_app_ctx_t;

static demo_app_ctx_t g_demo_ctx = {0};

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static void _demo_app_on_event(const event_t *event, void *user_data);
static void _demo_app_process_frame(void);
static void _demo_app_print_help(void);

// ==========================================================================
// 对外API实现
// ==========================================================================

int demo_app_init(const demo_app_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    memset(&g_demo_ctx, 0, sizeof(g_demo_ctx));
    g_demo_ctx.evt_bus = config->evt_bus;
    g_demo_ctx.data_bus = config->data_bus;
    g_demo_ctx.g_fsm = config->g_fsm;
    g_demo_ctx.cap_srv = config->cap_srv;
    g_demo_ctx.running = false;

    // 订阅事件
    event_subscriber_t sub = {0};
    sub.event_type = EVENT_TYPE_CAP_FRAME_READY; // 订阅帧就绪事件
    sub.callback = _demo_app_on_event;
    sub.user_data = NULL;

    g_demo_ctx.cap_sub_id = event_bus_subscribe(g_demo_ctx.evt_bus, &sub);
    if (g_demo_ctx.cap_sub_id < 0) {
        LOG_E("Demo App: Failed to subscribe to events");
        return -1;
    }

    LOG_I("Demo App: Initialized");
    _demo_app_print_help();
    return 0;
}

#include <signal.h>
extern volatile sig_atomic_t g_quit_flag; 
int demo_app_run(void)
{
    g_demo_ctx.running = true;
    LOG_I("Demo App: Running...");
    static int heartbeat = 0; // <-- 增加静态变量
    // 简单的主循环：处理用户输入 + 检查状态
    while (g_demo_ctx.running && !g_quit_flag) { // <-- 【修改】增加 !g_quit_flag 检查
        // 检查全局状态
        global_state_t g_state = global_fsm_get_state(g_demo_ctx.g_fsm);
        (void)g_state;
        
        // 【重要】每次 select 之前必须完全重置 fd_set 和 timeval！
        fd_set fds;
        struct timeval tv;
        
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms 超时
        // 【增加】心跳打印，每100次循环打印一次 (约1秒)
        if (heartbeat % 100 == 0) {
             LOG_D("Demo App: Heartbeat %d", heartbeat); // 如果你想确认主循环在跑，打开这行
        }
        heartbeat++;
        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char cmd = 0;
            // 【修改】read 改成循环读，把缓冲区读空，避免残留的 \n 影响
            while (read(STDIN_FILENO, &cmd, 1) > 0) {
                if (cmd == '\n' || cmd == '\r') continue;
                
                LOG_I("Demo App: Received key '%c'", cmd); // <-- 增加日志
                
                switch (cmd) {
                    case 's':
                    case 'S':
                        LOG_I("Demo App: User pressed START");
                        global_fsm_post_event(g_demo_ctx.g_fsm, GLOBAL_EVENT_SYSTEM_START);
                        break;
                    case 't':
                    case 'T':
                        LOG_I("Demo App: User pressed STOP");
                        global_fsm_post_event(g_demo_ctx.g_fsm, GLOBAL_EVENT_SYSTEM_STOP);
                        break;
                    case 'q':
                    case 'Q':
                        LOG_I("Demo App: User pressed QUIT");
                        global_fsm_post_event(g_demo_ctx.g_fsm, GLOBAL_EVENT_SYSTEM_SHUTDOWN);
                        g_demo_ctx.running = false;
                        break;
                    case 'h':
                    case 'H':
                        _demo_app_print_help();
                        break;
                    default:
                        LOG_W("Demo App: Unknown command '%c'", cmd);
                        _demo_app_print_help();
                        break;
                }
                break; // 只处理一个字符
            }
        } else if (ret < 0 && errno != EINTR) {
            LOG_E("Demo App: select error (errno=%d)", errno);
        }

        // 小睡一会，避免 CPU 100%
        usleep(1000);
    }

    LOG_I("Demo App: Main loop exited");
    return 0;
}

int demo_app_deinit(void)
{
    // 取消订阅
    if (g_demo_ctx.cap_sub_id >= 0) {
        event_bus_unsubscribe(g_demo_ctx.evt_bus, g_demo_ctx.cap_sub_id);
        g_demo_ctx.cap_sub_id = -1;
    }

    memset(&g_demo_ctx, 0, sizeof(g_demo_ctx));
    LOG_I("Demo App: Deinitialized");
    return 0;
}

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

static void _demo_app_on_event(const event_t *event, void *user_data)
{
    if (event == NULL) return;

    switch (event->type) {
        case EVENT_TYPE_CAP_FRAME_READY:
            // 收到帧就绪事件，去 Data Bus 取帧
            _demo_app_process_frame();
            break;
        default:
            break;
    }
}

static void _demo_app_process_frame(void)
{
    data_bus_item_handle_t item = NULL;
    
    // 获取最新帧
    int ret = data_bus_acquire_latest(g_demo_ctx.data_bus, DATA_TYPE_VIDEO_FRAME, &item);
    if (ret != 0 || item == NULL) {
        return;
    }

    // 获取帧信息
    data_bus_item_info_t info = {0};
    data_bus_get_item_info(item, &info);

    // 这里只是演示，实际项目中可以：
    // 1. 把帧传给显示服务
    // 2. 把帧传给 AI 服务
    // 3. 保存 YUV 文件
    LOG_D("Demo App: Processed frame (index=%d, ts=%lu, size=%u)", 
          0, // 实际可以从 data_ptr 里解析
          (unsigned long)info.timestamp, 
          info.data_size);

    // 释放帧
    data_bus_release(item);
}

static void _demo_app_print_help(void)
{
    LOG_I("========================================");
    LOG_I("  Demo App Control:");
    LOG_I("    [s] - Start system");
    LOG_I("    [t] - Stop system");
    LOG_I("    [q] - Quit application");
    LOG_I("    [h] - Show this help");
    LOG_I("========================================");
}
