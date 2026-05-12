/* SPDX-License-Identifier: MIT */
/**
 * @file main.c
 * @brief 轻量化系统主入口（纯底层框架，无业务代码）
 * @details 职责：日志/信号/双总线/优雅退出 初始化
 *          业务模块通过事件总线交互，main永久不变
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "log.h"
#include "vision_ai_config.h"
#include "main.h"
#include "initcall.h"

// 极简全局上下文（内部静态，对外完全隐藏）
static app_context_t g_app_ctx = {0};

// ==========================================================================
// 终端 底层基建（内部使用）
// ==========================================================================
static void app_set_terminal_noncanonical(void)
{
    struct termios new_termios;
    if (tcgetattr(STDIN_FILENO, &g_app_ctx.old_termios) == 0) {
        g_app_ctx.termios_saved = true;
        new_termios = g_app_ctx.old_termios;
        
        new_termios.c_lflag &= ~(ICANON | ECHO); 
        new_termios.c_cc[VMIN] = 1;
        new_termios.c_cc[VTIME] = 0;
        
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
        LOG_I("Main: Terminal set to non-canonical mode");
    } else {
        LOG_W("Main: Failed to set terminal mode");
    }
}

static void app_restore_terminal_safe(void)
{
    if (g_app_ctx.termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_app_ctx.old_termios);
        g_app_ctx.termios_saved = false;
    }
}

// ==========================================================================
// 退出Pipe 底层基建（系统级安全退出）
// ==========================================================================
static int app_exit_pipe_init(void)
{
    if(pipe(g_app_ctx.exit_pipe) < 0) {
        LOG_E("Main: Create exit pipe failed");
        return -1;
    }
    LOG_I("Main: Global exit pipe init success");
    return 0;
}

void app_trigger_soft_exit(void)
{
    char sig = 'E';
    (void)write(g_app_ctx.exit_pipe[1], &sig, 1);
    g_app_ctx.app_running = false;
}

static void app_exit_pipe_deinit(void)
{
    close(g_app_ctx.exit_pipe[0]);
    close(g_app_ctx.exit_pipe[1]);
    LOG_I("Main: Global exit pipe deinit success");
}

// ==========================================================================
// 信号处理（Ctrl+C / 崩溃 安全处理）
// ==========================================================================
static void _signal_handler(int sig)
{
    (void)sig;
    app_trigger_soft_exit();
}

static void _crash_signal_handler(int sig)
{
    (void)sig;
    const char *msg = "\n[Fatal] System crash, cleaning up...\n";
    (void)write(STDERR_FILENO, msg, strlen(msg));
    app_trigger_soft_exit();
    usleep(100000); 
    _exit(1);
}

static void _init_signal_handling(void)
{
    struct sigaction sa;
    sa.sa_handler = _signal_handler;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct sigaction sa_crash;
    sa_crash.sa_handler = _crash_signal_handler;
    sa_crash.sa_flags = 0;
    sigfillset(&sa_crash.sa_mask);
    sigaction(SIGABRT, &sa_crash, NULL);
    sigaction(SIGSEGV, &sa_crash, NULL);
    
    LOG_I("Main: Signal handler init success");
}

// ==========================================================================
// 双总线初始化（事件+数据，核心底层）
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
// 统一资源清理
// ==========================================================================
static void _cleanup_resources(void)
{
    LOG_I("Main: Starting resource cleanup...");

    app_restore_terminal_safe();
    
    // 销毁核心组件
    data_bus_deinit(g_app_ctx.data_bus);
    event_bus_deinit(g_app_ctx.evt_bus);
    app_exit_pipe_deinit();

    LOG_I("Main: Resource cleanup complete");
}

// ==========================================================================
// 主函数：纯底层框架，零业务代码
// ==========================================================================
int main(int argc, char **argv)
{
    memset(&g_app_ctx, 0, sizeof(g_app_ctx));
    g_app_ctx.app_running = true;

    // 1. 日志初始化
    log_init(LOG_LEVEL_INFO);
    LOG_I("Main: ========================================");
    LOG_I("Main: Vision AI Framework Starting...");
    LOG_I("Main: ========================================");

    // 2. 系统底层初始化
    _init_signal_handling();
    if(app_exit_pipe_init() < 0) goto error_exit;
    app_set_terminal_noncanonical();

    // 3. 核心双总线初始化
    if (_main_init_buses() != 0) goto error_exit;

    // 4. 自动加载所有业务模块
    do_initcalls();

    // 5. 底层等待退出（业务逻辑在app.c/服务中实现）
    LOG_I("Main: System running, waiting for exit signal...");
    while (g_app_ctx.app_running) {
        pause(); // 休眠等待信号，零CPU占用
    }

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