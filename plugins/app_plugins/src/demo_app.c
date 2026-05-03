// plugins/app_plugins/src/demo_app.c
#include "demo_app.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>

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

    // 订阅事件 (注意：现在只是注册，回调不会立即在发布线程执行)
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
int demo_app_run(void)
{
    g_demo_ctx.running = true;
    LOG_I("Demo App: Running...");
    
    extern volatile sig_atomic_t g_quit_flag;
    
    // 【新增】获取 Event Bus 的等待文件描述符
    int bus_fd = event_bus_get_wait_fd(g_demo_ctx.evt_bus);
    if (bus_fd < 0) {
        LOG_E("Demo App: Failed to get bus wait fd");
        return -1;
    }

    int timeout_count = 0;
    int max_fd = (STDIN_FILENO > bus_fd) ? STDIN_FILENO : bus_fd;

    while (g_demo_ctx.running && !g_quit_flag) 
    { 
        fd_set read_fds;
        struct timeval timeout;
        
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(bus_fd, &read_fds); // 【新增】监听 Bus FD
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 20000; // 20ms 超时

        // 【修改】nfds 必须是最大 fd + 1
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_E("Demo App: select error: %s", strerror(errno));
            break;
        } 
        else if (ret > 0) {
            // 【优先级1】检查 Event Bus 是否有事件
            if (FD_ISSET(bus_fd, &read_fds)) {
                // 【核心】分发事件，这会执行 _demo_app_on_event
                event_bus_dispatch(g_demo_ctx.evt_bus);
            }

            // 【优先级2】检查键盘是否有输入
            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                char cmd = 0;
                ssize_t nread = read(STDIN_FILENO, &cmd, 1);
                
                if (nread == 1) {
                    timeout_count = 0;
                    if (cmd == '\n' || cmd == '\r') {
                        continue;
                    } 
                    LOG_I("Demo App: Received key '%c'", cmd);
                    
                    switch (cmd) {
                        case 's': case 'S':
                            LOG_I("Demo App: User pressed START");
                            global_fsm_post_event(g_demo_ctx.g_fsm, GLOBAL_EVENT_SYSTEM_START);
                            break;
                        case 't': case 'T':
                            LOG_I("Demo App: User pressed STOP");
                            global_fsm_post_event(g_demo_ctx.g_fsm, GLOBAL_EVENT_SYSTEM_STOP);
                            break;
                        case 'q': case 'Q':
                            LOG_I("Demo App: User pressed QUIT");
                            g_demo_ctx.running = false; 
                            break;
                        case 'h': case 'H':
                            _demo_app_print_help();
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        else {
            // 超时逻辑
            timeout_count++;
            if (timeout_count % 100 == 0) {
                LOG_D("Demo App: Main thread alive, select timeout %d times", timeout_count);
            }
        }
    }

    LOG_I("Demo App: Main loop exited");
    return 0;
}

int demo_app_deinit(void)
{
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
            // 注意：这个函数现在运行在 Main Thread 了！
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