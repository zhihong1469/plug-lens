/* SPDX-License-Identifier: MIT */
/**
 * @file main.c
 * @brief 系统主入口（纯底层框架，无业务代码）
 * @details 负责：日志/信号/管道/终端/双总线/全局FSM 初始化
 *          业务模块通过 initcall 自动注册加载，main永久不变
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "log.h"
#include "module_fsm.h"
#include "vision_ai_config.h"
#include "main.h"
#include "event_bus.h"
#include "data_bus.h"
#include "initcall.h"  // 自动初始化头文件

// 全局唯一应用上下文（公共层实例化，无零散全局变量）
app_context_t g_app_ctx = {0};

// ==========================================================================
// 内部静态辅助函数声明（仅 main.c 内部使用）
// ==========================================================================
static int _main_init_buses(void);
static int _main_init_global_fsm(void);
static void _safe_stop_all_services(void);
static void _restore_terminal_mode(void);

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
// 信号处理
// ==========================================================================
static void _signal_handler(int sig)
{
    (void)sig;
    app_trigger_soft_exit();
}

static void _crash_signal_handler(int sig)
{
    const char *msg = "\n[Fatal] Main: Received crash signal, cleaning up...\n";
    (void)write(STDERR_FILENO, msg, strlen(msg));
    app_trigger_soft_exit();
    usleep(100000); 
    _exit(1);
}

static void _init_signal_handling(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = _signal_handler;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct sigaction sa_crash;
    memset(&sa_crash, 0, sizeof(sa_crash));
    sa_crash.sa_handler = _crash_signal_handler;
    sa_crash.sa_flags = 0;
    sigfillset(&sa_crash.sa_mask);

    sigaction(SIGABRT, &sa_crash, NULL);
    sigaction(SIGSEGV, &sa_crash, NULL);
    LOG_I("Main: Signal handler init success");
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
// 底层框架初始化：双总线
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
// 底层框架初始化：全局状态机
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
// 安全停止服务
// ==========================================================================
static void _safe_stop_all_services(void)
{
    LOG_I("Main: Safely stopping all services...");
    
    if (g_app_ctx.g_fsm) {
        global_state_t current_state = global_fsm_get_state(g_app_ctx.g_fsm);
        if (current_state == GLOBAL_STATE_RUNNING || current_state == GLOBAL_STATE_DEGRADED) {
            global_fsm_post_event(g_app_ctx.g_fsm, GLOBAL_EVENT_SYSTEM_STOP);
            usleep(200000); 
        }
    }
}

// ==========================================================================
// 统一资源清理
// ==========================================================================
static void _cleanup_resources(void)
{
    LOG_I("Main: Starting resource cleanup...");

    _restore_terminal_mode();
    _safe_stop_all_services();

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
// 主函数：纯框架，零业务代码
// ==========================================================================
int main(int argc, char **argv)
{
    memset(&g_app_ctx, 0, sizeof(g_app_ctx));
    g_app_ctx.app_running = true;

    // 1. 日志初始化
    log_init(LOG_LEVEL_DEBUG);
    LOG_I("Main: ========================================");
    LOG_I("Main: Vision AI Framework Starting...");
    LOG_I("Main: ========================================");

    // 2. 系统基建初始化
    _init_signal_handling();
    if(app_exit_pipe_init() < 0) goto error_exit;
    app_set_terminal_noncanonical();

    // 3. 底层核心框架初始化
    if (_main_init_buses() != 0) goto error_exit;
    if (_main_init_global_fsm() != 0) goto error_exit;

    // ====================== 核心 ======================
    // 4. 自动加载所有业务模块（main无需修改）
    // ====================== 核心 ======================
    do_initcalls();

    // 5. 启动主循环
    LOG_I("Main: Entering main loop...");
    demo_app_run();

    // 正常退出
    LOG_I("Main: Application exited normally");
    _cleanup_resources();
    log_deinit();
    return 0;

error_exit:
    LOG_E("Main: Application exited with error");
    _cleanup_resources();
    log_deinit();
    return -1;
}