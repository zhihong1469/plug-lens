#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "log.h"
#include "module_fsm.h"
#include "demo_app.h"
#include "vision_ai_config.h"
#include "main.h"
#include "face_detect_srv.h"

// 全局唯一应用上下文（公共层实例化，无零散全局变量）
app_context_t g_app_ctx = {0};

// ==========================================================================
// 内部静态辅助函数声明（仅 main.c 内部使用）
// ==========================================================================
static int _main_init_buses(void);
static int _main_init_global_fsm(void);
static int _main_init_capture_service(void);
static int _main_init_face_detect_service(void);
static int _main_init_demo_application(void);
static void _safe_stop_all_services(void); // 【新增】安全停止所有服务

// ==========================================================================
// 终端 公共基建实现
// ==========================================================================
void app_set_terminal_noncanonical(void)
{
    struct termios new_termios;
    if (tcgetattr(STDIN_FILENO, &g_app_ctx.old_termios) == 0) {
        g_app_ctx.termios_saved = true;
        new_termios = g_app_ctx.old_termios;
        
        new_termios.c_lflag &= ~(ICANON | ECHO); 
        new_termios.c_cc[VMIN] = 1;
        new_termios.c_cc[VTIME] = 0;
        
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
        atexit(app_restore_terminal_safe);
        LOG_I("Main: Terminal set to non-canonical mode");
    } else {
        LOG_W("Main: Failed to set terminal mode (tcgetattr error: %s)", strerror(errno));
    }
}

void app_restore_terminal_safe(void)
{
    if (g_app_ctx.termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_app_ctx.old_termios);
        g_app_ctx.termios_saved = false;
        fprintf(stderr, "\n[System] Terminal restored (atexit fallback).\n");
    }
}

static void _restore_terminal_mode(void)
{
    if (g_app_ctx.termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_app_ctx.old_termios);
        g_app_ctx.termios_saved = false;
        LOG_I("Main: Terminal restored");
    }
}

// ==========================================================================
// 退出Pipe 公共基建实现
// ==========================================================================
int app_exit_pipe_init(void)
{
    if(pipe(g_app_ctx.exit_pipe) < 0) {
        LOG_E("Main: Create exit pipe failed, errno=%d", errno);
        return -1;
    }
    LOG_I("Main: Global exit pipe init success");
    return 0;
}

void app_trigger_soft_exit(void)
{
    // 异步信号安全：仅向管道写入1字节，触发所有监听线程退出
    char sig = 'E';
    (void)write(g_app_ctx.exit_pipe[1], &sig, 1);
    g_app_ctx.app_running = false;
}

void app_exit_pipe_deinit(void)
{
    if(g_app_ctx.exit_pipe[0] > 0) close(g_app_ctx.exit_pipe[0]);
    if(g_app_ctx.exit_pipe[1] > 0) close(g_app_ctx.exit_pipe[1]);
    memset(g_app_ctx.exit_pipe, 0, sizeof(g_app_ctx.exit_pipe));
    LOG_I("Main: Global exit pipe deinit success");
}

// ==========================================================================
// 【核心完善】信号处理（新增 SIGABRT/SIGSEGV 捕获）
// ==========================================================================
static void _signal_handler(int sig)
{
    (void)sig;
    // 唯一操作：触发全局软退出，无任何不安全调用
    app_trigger_soft_exit();
}

// 【新增】崩溃信号专用处理（安全打印+清理）
static void _crash_signal_handler(int sig)
{
    // 异步信号安全：仅使用 write() 和 _exit()
    const char *msg = "\n[Fatal] Main: Received crash signal, cleaning up...\n";
    (void)write(STDERR_FILENO, msg, strlen(msg));
    
    // 触发软退出
    app_trigger_soft_exit();
    
    // 给系统一点时间清理（非信号安全，但为了调试日志）
    usleep(100000); 
    
    // 强制退出，避免递归崩溃
    _exit(1);
}

static void _init_signal_handling(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = _signal_handler;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);

    // 正常退出信号
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        LOG_E("Main: Failed to register SIGINT handler");
    } else {
        LOG_I("Main: SIGINT(Ctrl+C) handler registered");
    }

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        LOG_E("Main: Failed to register SIGTERM handler");
    } else {
        LOG_I("Main: SIGTERM(kill) handler registered");
    }

    // 【新增】崩溃信号捕获
    struct sigaction sa_crash;
    memset(&sa_crash, 0, sizeof(sa_crash));
    sa_crash.sa_handler = _crash_signal_handler;
    sa_crash.sa_flags = 0;
    sigfillset(&sa_crash.sa_mask);

    if (sigaction(SIGABRT, &sa_crash, NULL) != 0) {
        LOG_W("Main: Failed to register SIGABRT handler (non-fatal)");
    } else {
        LOG_I("Main: SIGABRT(crash) handler registered");
    }

    if (sigaction(SIGSEGV, &sa_crash, NULL) != 0) {
        LOG_W("Main: Failed to register SIGSEGV handler (non-fatal)");
    } else {
        LOG_I("Main: SIGSEGV(segfault) handler registered");
    }
}

// ==========================================================================
// Global FSM 回调适配层
// ==========================================================================
static void _main_on_g_fsm_state_change(global_state_t old_state,
                                          global_state_t new_state,
                                          void *user_data)
{
    (void)user_data;
    if (g_app_ctx.evt_bus != NULL) {
        event_t evt = {0};
        evt.type = EVENT_TYPE_SYS_STATE_CHANGED;
        evt.source = "global_fsm";
        evt.data = &new_state;
        evt.data_len = sizeof(new_state);
        event_bus_publish(g_app_ctx.evt_bus, &evt);
    }
}

static void _main_on_g_fsm_event(global_event_t event,
                                  const char *module_name,
                                  void *user_data)
{
    (void)user_data;
    if (g_app_ctx.evt_bus == NULL) return;

    event_type_t evt_type = EVENT_TYPE_INVALID;
    switch (event) {
        case GLOBAL_EVENT_MODULE_READY:   evt_type = EVENT_TYPE_MOD_READY; break;
        case GLOBAL_EVENT_MODULE_RUNNING: evt_type = EVENT_TYPE_MOD_RUNNING; break;
        case GLOBAL_EVENT_MODULE_ERROR:   evt_type = EVENT_TYPE_MOD_ERROR; break;
        default: break;
    }
    
    if (evt_type != EVENT_TYPE_INVALID) {
        event_bus_publish_simple(g_app_ctx.evt_bus, evt_type, module_name);
    }
}

// ==========================================================================
// 3.【封装】双总线初始化（事件总线 + 数据总线）
// ==========================================================================
static int _main_init_buses(void)
{
    int ret = 0;

    LOG_I("Main: Initializing Event Bus...");
    event_bus_config_t evt_bus_cfg = {0};
    evt_bus_cfg.max_subscribers = CONFIG_EVENT_BUS_MAX_SUBSCRIBERS;
    ret = event_bus_init(&evt_bus_cfg, &g_app_ctx.evt_bus);
    if (ret != 0) {
        LOG_E("Main: Failed to init Event Bus");
        return -1;
    }

    LOG_I("Main: Initializing Data Bus...");
    data_bus_config_t data_bus_cfg = {0};
    data_bus_cfg.max_items = CONFIG_DATA_BUS_MAX_FRAMES;
    data_bus_cfg.max_item_size = 2 * 1024 * 1024;
    data_bus_cfg.max_subscribers = 16;
    ret = data_bus_init(&data_bus_cfg, &g_app_ctx.data_bus);
    if (ret != 0) {
        LOG_E("Main: Failed to init Data Bus");
        return -1;
    }

    return 0;
}

// ==========================================================================
// 4.【封装】全局状态机初始化
// ==========================================================================
static int _main_init_global_fsm(void)
{
    int ret = 0;
    LOG_I("Main: Initializing Global FSM...");
    
    global_fsm_config_t g_fsm_cfg = {0};
    g_fsm_cfg.max_modules = CONFIG_GLOBAL_FSM_MAX_MODULES;
    g_fsm_cfg.state_cb = _main_on_g_fsm_state_change;
    g_fsm_cfg.event_cb = _main_on_g_fsm_event;
    g_fsm_cfg.user_data = NULL;
    
    ret = global_fsm_init(&g_fsm_cfg, &g_app_ctx.g_fsm);
    if (ret != 0) {
        LOG_E("Main: Failed to init Global FSM");
        return -1;
    }

    return 0;
}

// ==========================================================================
// 5.1【封装】采集服务初始化 + 子状态机注册
// ==========================================================================
static int _main_init_capture_service(void)
{
    int ret = 0;
    LOG_I("Main: Initializing Capture Service...");

    capture_srv_config_t cap_srv_cfg = {0};
    cap_srv_cfg.link_cfg.hal_config.dev_path = CONFIG_CAPTURE_DEV_PATH;
    cap_srv_cfg.link_cfg.hal_config.width = CONFIG_CAPTURE_WIDTH;
    cap_srv_cfg.link_cfg.hal_config.height = CONFIG_CAPTURE_HEIGHT;
    cap_srv_cfg.link_cfg.hal_config.fps = CONFIG_CAPTURE_FPS;
    cap_srv_cfg.link_cfg.hal_config.format = CONFIG_CAPTURE_FORMAT;
    cap_srv_cfg.link_cfg.hal_config.buf_count = CONFIG_CAPTURE_BUF_COUNT;
    cap_srv_cfg.link_cfg.hal_config.lock_exposure = CONFIG_CAPTURE_LOCK_EXPOSURE;
    cap_srv_cfg.link_cfg.hal_config.lock_white_balance = CONFIG_CAPTURE_LOCK_WHITE_BALANCE;
    cap_srv_cfg.link_cfg.hal_config.lock_gain = CONFIG_CAPTURE_LOCK_GAIN;
    cap_srv_cfg.link_cfg.frame_pool_size = CONFIG_FRAME_LINK_POOL_SIZE;
    cap_srv_cfg.link_cfg.queue_size = CONFIG_FRAME_LINK_QUEUE_SIZE;
    
    cap_srv_cfg.evt_bus = g_app_ctx.evt_bus;
    cap_srv_cfg.data_bus = g_app_ctx.data_bus;
    cap_srv_cfg.callbacks.state_change_cb = global_fsm_on_module_state_change;
    cap_srv_cfg.callbacks.user_data = g_app_ctx.g_fsm;
    cap_srv_cfg.auto_start = false;

    ret = capture_srv_create(&cap_srv_cfg, &g_app_ctx.cap_srv);
    if (ret != 0) {
        LOG_E("Main: Failed to create Capture Service");
        return -1;
    }

    // 注册子状态机到全局状态机
    module_fsm_handle_t cap_fsm = capture_srv_get_fsm(g_app_ctx.cap_srv);
    global_fsm_register_module(g_app_ctx.g_fsm, "capture_srv", cap_fsm, true);

    return 0;
}

// ==========================================================================
// 5.2【封装】人脸检测服务初始化 + 子状态机注册
// ==========================================================================
static int _main_init_face_detect_service(void)
{
    int ret = 0;
    LOG_I("Main: Initializing Face Detect Service...");

    face_detect_srv_config_t face_srv_cfg = {0};
    face_srv_cfg.model_path = CONFIG_AI_MODEL_PATH;
    face_srv_cfg.ai_input_w = CONFIG_AI_INPUT_W;
    face_srv_cfg.ai_input_h = CONFIG_AI_INPUT_H;
    face_srv_cfg.score_threshold = CONFIG_AI_SCORE_THRESH;
    face_srv_cfg.iou_threshold = CONFIG_AI_IOU_THRESH;
    
    face_srv_cfg.evt_bus = g_app_ctx.evt_bus;
    face_srv_cfg.data_bus = g_app_ctx.data_bus;
    face_srv_cfg.callbacks.state_change_cb = global_fsm_on_module_state_change;
    face_srv_cfg.callbacks.user_data = g_app_ctx.g_fsm;
    face_srv_cfg.auto_start = false;

    ret = face_detect_srv_create(&face_srv_cfg, &g_app_ctx.face_detect_srv);
    if (ret != 0) {
        LOG_E("Main: Failed to create Face Detect Service");
        return -1;
    }

    module_fsm_handle_t face_fsm = face_detect_srv_get_fsm(g_app_ctx.face_detect_srv);
    global_fsm_register_module(g_app_ctx.g_fsm, "face_detect_srv", face_fsm, true);

    return 0;
}

// ==========================================================================
// 6.【封装】Demo App 初始化
// ==========================================================================
static int _main_init_demo_application(void)
{
    int ret = 0;
    LOG_I("Main: Initializing Demo App...");

    demo_app_config_t app_cfg = {0};
    app_cfg.evt_bus = g_app_ctx.evt_bus;
    app_cfg.data_bus = g_app_ctx.data_bus;
    app_cfg.g_fsm = g_app_ctx.g_fsm;
    app_cfg.cap_srv = g_app_ctx.cap_srv;
    app_cfg.exit_pipe_read_fd = g_app_ctx.exit_pipe[0];

    ret = demo_app_init(&app_cfg);
    if (ret != 0) {
        LOG_E("Main: Failed to init Demo App");
        return -1;
    }

    return 0;
}

// ==========================================================================
// 【新增】安全停止所有服务（避免状态机 invalid transition）
// ==========================================================================
static void _safe_stop_all_services(void)
{
    LOG_I("Main: Safely stopping all services...");
    
    // 1. 先通过 Global FSM 发送 STOP 事件（让服务从 RUNNING -> READY）
    if (g_app_ctx.g_fsm) {
        global_state_t current_state = global_fsm_get_state(g_app_ctx.g_fsm);
        if (current_state == GLOBAL_STATE_RUNNING || current_state == GLOBAL_STATE_DEGRADED) {
            LOG_I("Main: Posting SYSTEM_STOP to Global FSM...");
            global_fsm_post_event(g_app_ctx.g_fsm, GLOBAL_EVENT_SYSTEM_STOP);
            // 给一点时间让状态机流转（非阻塞，仅为了日志完整）
            usleep(200000); 
        }
    }
}

// ==========================================================================
// 统一资源清理（基建层收口，顺序可控）
// ==========================================================================
static void _cleanup_resources(void)
{
    LOG_I("Main: Starting resource cleanup...");

    _restore_terminal_mode();
    demo_app_deinit();

    // 【新增】第一步：安全停止服务（RUNNING -> READY）
    _safe_stop_all_services();

    // 第二步：销毁服务（READY -> DEINIT）
    // 必须先销毁生成服务，再销毁消费服务，然后是总线和全局状态机
    if (g_app_ctx.face_detect_srv) {
        face_detect_srv_destroy(g_app_ctx.face_detect_srv);
        g_app_ctx.face_detect_srv = NULL;
    }
    if (g_app_ctx.cap_srv) {
        capture_srv_destroy(g_app_ctx.cap_srv);
        g_app_ctx.cap_srv = NULL;
    }

    if (g_app_ctx.g_fsm) {
        global_fsm_deinit(g_app_ctx.g_fsm);
        g_app_ctx.g_fsm = NULL;
    }
    if (g_app_ctx.data_bus) {
        data_bus_deinit(g_app_ctx.data_bus);
        g_app_ctx.data_bus = NULL;
    }
    if (g_app_ctx.evt_bus) {
        event_bus_deinit(g_app_ctx.evt_bus);
        g_app_ctx.evt_bus = NULL;
    }

    app_exit_pipe_deinit();
    LOG_I("Main: Resource cleanup complete");
}

// ==========================================================================
// 主函数：纯架构流水线，零业务逻辑（极致简洁）
// ==========================================================================
int main(int argc, char **argv)
{
    memset(&g_app_ctx, 0, sizeof(g_app_ctx));
    g_app_ctx.app_running = true;

    // 1. 日志初始化
    log_init(LOG_LEVEL_DEBUG);
    LOG_I("Main: ========================================");
    LOG_I("Main: Vision AI Application Starting...");
    LOG_I("Main: ========================================");

    // 2. 基建层初始化（信号 -> 管道 -> 终端）
    _init_signal_handling();
    if(app_exit_pipe_init() < 0) goto error_exit;
    app_set_terminal_noncanonical();

    // 3. 初始化双总线（封装函数）
    if (_main_init_buses() != 0) goto error_exit;

    // 4. 初始化全局状态机（封装函数）
    if (_main_init_global_fsm() != 0) goto error_exit;

    // 5. 初始化采集服务（封装函数）
    if (_main_init_capture_service() != 0) goto error_exit;
    // if (_main_init_face_detect_service() != 0) goto error_exit;

    // 6. 初始化业务应用（封装函数）
    if (_main_init_demo_application() != 0) goto error_exit;

    // 7. 启动业务主循环
    LOG_I("Main: Entering main loop...");
    demo_app_run();

    // 8. 正常退出
    LOG_I("Main: ========================================");
    LOG_I("Main: Application exited normally");
    LOG_I("Main: ========================================");
    _cleanup_resources();
    log_deinit();
    return 0;

error_exit:
    LOG_E("Main: ========================================");
    LOG_E("Main: Application exited with error");
    LOG_E("Main: ========================================");
    _cleanup_resources();
    log_deinit();
    return -1;
}