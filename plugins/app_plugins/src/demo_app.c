#include "demo_app.h"
#include "log.h"
#include "main.h"   // 引入全局app上下文
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>

// ==========================================================================
// 内部私有上下文
// ==========================================================================
typedef struct {
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;
    global_fsm_handle_t g_fsm;
    capture_srv_handle_t cap_srv;
    
    int exit_pipe_read_fd;  // 退出管道读端
    int cap_sub_id;         // 采集事件订阅ID
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
    g_demo_ctx.exit_pipe_read_fd = config->exit_pipe_read_fd;

    // 订阅帧就绪事件
    event_subscriber_t sub = {0};
    sub.event_type = EVENT_TYPE_CAP_FRAME_READY;
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

int demo_app_run(void)
{
    LOG_I("Demo App: Running...");
    
    // 获取Event Bus等待fd
    int bus_fd = event_bus_get_wait_fd(g_demo_ctx.evt_bus);
    if (bus_fd < 0) {
        LOG_E("Demo App: Failed to get bus wait fd");
        return -1;
    }

    int timeout_count = 0;
    // 计算最大fd
    int max_fd = STDIN_FILENO;
    if(bus_fd > max_fd) max_fd = bus_fd;
    if(g_demo_ctx.exit_pipe_read_fd > max_fd) max_fd = g_demo_ctx.exit_pipe_read_fd;

    // 核心：全局运行标志（volatile，线程安全）
    while (g_app_ctx.app_running) 
    { 
        fd_set read_fds;
        struct timeval timeout;
        
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(bus_fd, &read_fds);
        FD_SET(g_demo_ctx.exit_pipe_read_fd, &read_fds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 40000; // 20ms超时

        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        // ============== 修复：信号中断直接退出 ==============
        if (ret < 0) {
            if (errno == EINTR) {
                LOG_I("Demo App: Select interrupted by signal, exiting...");
                break;
            }
            LOG_E("Demo App: select error: %s", strerror(errno));
            break;
        }

        // ============== 修复：最高优先级处理退出管道 ==============
        if (ret > 0 && FD_ISSET(g_demo_ctx.exit_pipe_read_fd, &read_fds)) {
            LOG_I("Demo App: Received global exit signal, exiting main loop");
            // 直接终止循环，不再处理任何事件
            break;
        }

        // 正常事件处理
        if (ret > 0) {
            // Event Bus事件分发
            if (FD_ISSET(bus_fd, &read_fds)) {
                event_bus_dispatch(g_demo_ctx.evt_bus);
            }

            // 键盘命令解析
            if (FD_ISSET(STDIN_FILENO, &read_fds)) {
                char cmd = 0;
                ssize_t nread = read(STDIN_FILENO, &cmd, 1);
                
                if (nread == 1) {
                    timeout_count = 0;
                    if (cmd == '\n' || cmd == '\r') continue;
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
                            app_trigger_soft_exit();
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
            // 超时保活
            timeout_count++;
            if (timeout_count % 200 == 0) {
                LOG_D("Demo App: Main thread alive");
            }
        }
    }

    LOG_I("Demo App: Main loop exited gracefully");
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
// 用户回调函数,调用时处于Event Bus rdlock 期间。该函数请尽可能简单。如果用户回调内部尝试获取写锁或其他锁，可能导致死锁。
static void _demo_app_on_event(const event_t *event, void *user_data)
{
    if (event == NULL) return;

    switch (event->type) {
        case EVENT_TYPE_CAP_FRAME_READY:
            _demo_app_process_frame();
            break;
        default:
            break;
    }
}

static void _demo_app_process_frame(void)
{
    data_bus_item_handle_t item = NULL;
    int ret = data_bus_acquire_latest(g_demo_ctx.data_bus, DATA_TYPE_VIDEO_FRAME, &item);
    if (ret != 0 || item == NULL) {
        return;
    }

    data_bus_item_info_t info = {0};
    data_bus_get_item_info(item, &info);

    LOG_D("Demo App: Processed frame (ts=%lu, size=%u)", 
          (unsigned long)info.timestamp, 
          info.data_size);

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