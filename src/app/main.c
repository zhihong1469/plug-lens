#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "log.h"
#include "event_bus.h"
#include "data_bus.h"
#include "global_fsm.h"
#include "module_fsm.h"
#include "capture_srv.h"
#include "demo_app.h"
#include "vision_ai_config.h" 
#include <termios.h> 

// ==========================================================================
// 全局句柄
// ==========================================================================
event_bus_handle_t g_evt_bus = NULL;
data_bus_handle_t g_data_bus = NULL;
global_fsm_handle_t g_g_fsm = NULL;
capture_srv_handle_t g_cap_srv = NULL;

// ==========================================================================
// 【核心】全局退出标志（必须是 volatile sig_atomic_t）
// ==========================================================================
volatile sig_atomic_t g_quit_flag = 0;

// ==========================================================================
// 终端模式管理
// ==========================================================================
static struct termios g_old_termios;
static bool g_termios_saved = false;

/**
 * @brief 【异步信号不安全】恢复终端模式
 * 
 * 注意：这个函数不能在信号处理函数里调用！
 * 只能在主线程的正常退出流程里调用。
 */
static void _restore_terminal_mode(void)
{
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_old_termios);
        g_termios_saved = false;
        LOG_I("Main: Terminal restored");
    }
}

/**
 * @brief 【atexit 专用】最后的终端恢复保障
 * 
 * 注意：这里用 fprintf 而不是 LOG_I，因为 log 可能已经 deinit 了。
 */
static void _restore_terminal_mode_atexit(void)
{
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_old_termios);
        g_termios_saved = false;
        fprintf(stderr, "\n[System] Terminal restored (atexit fallback).\n");
    }
}

/**
 * @brief 设置终端为非规范模式（无回显、立即响应）
 */
static void _set_noncanonical_mode(void)
{
    struct termios new_termios;
    if (tcgetattr(STDIN_FILENO, &g_old_termios) == 0) {
        g_termios_saved = true;
        new_termios = g_old_termios;
        
        // 禁用规范模式和回显
        new_termios.c_lflag &= ~(ICANON | ECHO); 
        
        // VMIN=1, VTIME=0 -> 只要有数据就返回
        new_termios.c_cc[VMIN] = 1;
        new_termios.c_cc[VTIME] = 0;
        
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
        
        // 注册 atexit 作为最后的保障
        atexit(_restore_terminal_mode_atexit);
        
        LOG_I("Main: Terminal set to non-canonical mode");
    } else {
        LOG_W("Main: Failed to set terminal mode (tcgetattr error: %s)", strerror(errno));
    }
}

// ==========================================================================
// 【核心】信号处理函数（必须是异步信号安全的！）
// 
// 规则：
// ❌ 绝对禁止：拿锁、malloc/free、printf、LOG_I、tcsetattr 等
// ✅ 只允许：给 volatile sig_atomic_t 变量赋值
// ==========================================================================
static void _signal_handler(int sig)
{
    (void)sig; // 避免未使用参数警告
    
    // 【只做这一件事！】设置退出标志
    g_quit_flag = 1;
}

/**
 * @brief 初始化信号处理（使用 sigaction，比 signal() 更可靠）
 */
static void _init_signal_handling(void)
{
    struct sigaction sa;

    // 1. 初始化 sigaction 结构体
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = _signal_handler;
    sa.sa_flags = 0; // 不要用 SA_RESTART，否则 select 会被自动重启

    // 2. 屏蔽所有其他信号（防止信号处理函数被中断）
    sigfillset(&sa.sa_mask);

    // 3. 注册 SIGINT (Ctrl+C)
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        LOG_E("Main: Failed to register SIGINT handler (error: %s)", strerror(errno));
    } else {
        LOG_I("Main: SIGINT handler registered (Ctrl+C will set quit flag)");
    }

    // 4. 注册 SIGTERM (kill 命令)
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        LOG_E("Main: Failed to register SIGTERM handler (error: %s)", strerror(errno));
    } else {
        LOG_I("Main: SIGTERM handler registered");
    }
}

// ==========================================================================
// Global FSM 回调 -> Event Bus 适配层
// ==========================================================================
static void _main_on_g_fsm_state_change(global_state_t old_state,
                                          global_state_t new_state,
                                          void *user_data)
{
    (void)user_data;
    
    // 全局状态变化，发布到 Event Bus
    if (g_evt_bus != NULL) {
        event_t evt = {0};
        evt.type = EVENT_TYPE_SYS_STATE_CHANGED;
        evt.source = "global_fsm";
        evt.data = &new_state;
        evt.data_len = sizeof(new_state);
        event_bus_publish(g_evt_bus, &evt);
    }
}

static void _main_on_g_fsm_event(global_event_t event,
                                  const char *module_name,
                                  void *user_data)
{
    (void)user_data;
    
    // 全局事件，转换后发布到 Event Bus
    if (g_evt_bus == NULL) return;

    event_type_t evt_type = EVENT_TYPE_INVALID;
    switch (event) {
        case GLOBAL_EVENT_MODULE_READY:   evt_type = EVENT_TYPE_MOD_READY; break;
        case GLOBAL_EVENT_MODULE_RUNNING: evt_type = EVENT_TYPE_MOD_RUNNING; break;
        case GLOBAL_EVENT_MODULE_ERROR:   evt_type = EVENT_TYPE_MOD_ERROR; break;
        default: break;
    }
    
    if (evt_type != EVENT_TYPE_INVALID) {
        event_bus_publish_simple(g_evt_bus, evt_type, module_name);
    }
}

// ==========================================================================
// 【辅助】统一清理资源
// ==========================================================================
static void _cleanup_resources(void)
{
    LOG_I("Main: Starting resource cleanup...");

    // 1. 恢复终端（必须在最前面，防止 log 关闭后无法恢复）
    _restore_terminal_mode();

    // 2. 停止 Demo App
    demo_app_deinit();

    // 3. 销毁 Capture Service
    if (g_cap_srv) {
        capture_srv_destroy(g_cap_srv);
        g_cap_srv = NULL;
    }

    // 4. 销毁 FSM 和总线
    if (g_g_fsm) {
        global_fsm_deinit(g_g_fsm);
        g_g_fsm = NULL;
    }
    if (g_data_bus) {
        data_bus_deinit(g_data_bus);
        g_data_bus = NULL;
    }
    if (g_evt_bus) {
        event_bus_deinit(g_evt_bus);
        g_evt_bus = NULL;
    }

    LOG_I("Main: Resource cleanup complete");
}

// ==========================================================================
// 主函数
// ==========================================================================
int main(int argc, char **argv)
{
    int ret = 0;

    // 1. 初始化日志
    log_init(LOG_LEVEL_DEBUG);
    LOG_I("Main: ========================================");
    LOG_I("Main: Vision AI Application Starting...");
    LOG_I("Main: ========================================");

    // 2. 初始化信号处理（必须在最前面！）
    _init_signal_handling();

    // 3. 设置终端为非规范模式
    _set_noncanonical_mode();

    // -------------------------------------------------------------------------
    // 4. 初始化双总线
    // -------------------------------------------------------------------------
    LOG_I("Main: Initializing Event Bus...");
    event_bus_config_t evt_bus_cfg = {0};
    evt_bus_cfg.max_subscribers = CONFIG_EVENT_BUS_MAX_SUBSCRIBERS;
    ret = event_bus_init(&evt_bus_cfg, &g_evt_bus);
    if (ret != 0) {
        LOG_E("Main: Failed to init Event Bus");
        goto error_exit;
    }

    LOG_I("Main: Initializing Data Bus...");
    data_bus_config_t data_bus_cfg = {0};
    data_bus_cfg.max_items = CONFIG_DATA_BUS_MAX_FRAMES;
    data_bus_cfg.max_item_size = 2 * 1024 * 1024; // 2 MB
    ret = data_bus_init(&data_bus_cfg, &g_data_bus);
    if (ret != 0) {
        LOG_E("Main: Failed to init Data Bus");
        goto error_exit;
    }

    // -------------------------------------------------------------------------
    // 5. 初始化 Global FSM
    // -------------------------------------------------------------------------
    LOG_I("Main: Initializing Global FSM...");
    global_fsm_config_t g_fsm_cfg = {0};
    g_fsm_cfg.max_modules = CONFIG_GLOBAL_FSM_MAX_MODULES;
    g_fsm_cfg.state_cb = _main_on_g_fsm_state_change;
    g_fsm_cfg.event_cb = _main_on_g_fsm_event;
    g_fsm_cfg.user_data = NULL;
    ret = global_fsm_init(&g_fsm_cfg, &g_g_fsm);
    if (ret != 0) {
        LOG_E("Main: Failed to init Global FSM");
        goto error_exit;
    }

    // -------------------------------------------------------------------------
    // 6. 初始化业务服务（Capture Service）
    // -------------------------------------------------------------------------
    LOG_I("Main: Initializing Capture Service...");
    
    // 构建 Capture Service 配置
    capture_srv_config_t cap_srv_cfg = {0};
    
    // 填充 Link层 配置
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
    
    // 注入双总线句柄
    cap_srv_cfg.evt_bus = g_evt_bus;
    cap_srv_cfg.data_bus = g_data_bus;
    
    // 注入 Global FSM 回调
    cap_srv_cfg.callbacks.state_change_cb = global_fsm_on_module_state_change;
    cap_srv_cfg.callbacks.user_data = g_g_fsm;
    cap_srv_cfg.auto_start = false;

    // 创建 Capture Service
    ret = capture_srv_create(&cap_srv_cfg, &g_cap_srv);
    if (ret != 0) {
        LOG_E("Main: Failed to create Capture Service");
        goto error_exit;
    }

    // 注册到 Global FSM
    module_fsm_handle_t cap_fsm = capture_srv_get_fsm(g_cap_srv);
    global_fsm_register_module(g_g_fsm, "capture_srv", cap_fsm, true);

    // -------------------------------------------------------------------------
    // 7. 初始化 Demo App
    // -------------------------------------------------------------------------
    LOG_I("Main: Initializing Demo App...");
    demo_app_config_t app_cfg = {0};
    app_cfg.evt_bus = g_evt_bus;
    app_cfg.data_bus = g_data_bus;
    app_cfg.g_fsm = g_g_fsm;
    app_cfg.cap_srv = g_cap_srv;
    ret = demo_app_init(&app_cfg);
    if (ret != 0) {
        LOG_E("Main: Failed to init Demo App");
        goto error_exit;
    }

    // -------------------------------------------------------------------------
    // 8. 主循环
    // -------------------------------------------------------------------------
    LOG_I("Main: Entering main loop...");
    demo_app_run();

    // -------------------------------------------------------------------------
    // 9. 正常退出流程
    // -------------------------------------------------------------------------
    LOG_I("Main: ========================================");
    LOG_I("Main: Application exited normally");
    LOG_I("Main: ========================================");
    
    _cleanup_resources();
    log_deinit();
    return 0;

    // -------------------------------------------------------------------------
    // 错误退出流程
    // -------------------------------------------------------------------------
error_exit:
    LOG_E("Main: ========================================");
    LOG_E("Main: Application exited with error");
    LOG_E("Main: ========================================");
    
    _cleanup_resources();
    log_deinit();
    return -1;
}