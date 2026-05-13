/* SPDX-License-Identifier: MIT */
/**
 * @file main.c
 * @brief 轻量化系统主入口（纯底层框架，无业务代码）
 * @details 职责：日志/信号/双总线/优雅退出 初始化
 *          业务模块通过事件总线交互，main永久不变
 *          仅处理系统级异常,并通过事件总线发布通知,业务模块自行处理
 * @author Luo
 * @date 2026-05-31
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <termios.h>
#include "log.h"
#include "vision_ai_config.h"
#include "initcall.h"
#include "event_bus.h"
#include "data_bus.h"

// ==================================================================================
// 【框架约定】系统级总线固定名称（全局唯一，业务模块统一订阅）
// ==================================================================================
#define SYS_EVENT_BUS_NAME    "sys_event"    // 系统事件总线名称
#define SYS_DATA_BUS_NAME     "sys_data"     // 系统数据总线名称

// ==================================================================================
// 全局上下文：仅保留系统底层资源，无任何业务变量 + 无总线句柄（已删除）
// 对外隐藏，具体共用指针由初始化完成后对应模块执行保留句柄,模块内部API获取句柄，彻底杜绝全局变量滥用
// ==================================================================================
typedef struct {
    // 系统级优雅退出管道（线程/信号安全）
    int                     exit_pipe[2];

    // 终端配置（调试用）
    struct termios          old_termios;
    bool                    termios_saved;

    // main内部运行标记
    volatile bool           app_running;
} app_context_t;

// 极简全局上下文（内部静态，对外完全隐藏）
static app_context_t g_app_ctx = {0};

// ==========================================================================
// 【新增】Main层私有：系统事件统一发布接口（纯底层，无业务逻辑）
// 遵循V4.0：Main仅发布系统级事件，不感知业务模块 | 适配新总线：按名称发布
// ==========================================================================
static void app_publish_sys_event(event_type_t type)
{
    // 直接通过总线名称调用，main不持有任何句柄
    int ret = event_bus_publish_simple(SYS_EVENT_BUS_NAME, type, "MAIN");
    if (ret != 0) {
        LOG_E("Main: Failed to publish sys event: %s", event_type_to_str(type));
    } else {
        LOG_I("Main: Published sys event: %s", event_type_to_str(type));
    }
}

// ==========================================================================
// 终端 底层基建（内部使用）
// ==========================================================================
static void app_set_terminal_noncanonical(void)
{
    struct termios new_termios;
    if (tcgetattr(STDIN_FILENO, &g_app_ctx.old_termios) == 0) {
        g_app_ctx.termios_saved = true;
        new_termios = g_app_ctx.old_termios;
        
        // 修复BUG：非规范模式标准配置，移除重复赋值
        new_termios.c_lflag &= ~(ICANON | ECHO); 
        new_termios.c_cc[VMIN] = 0;
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
    
    // 【新增】触发软退出时，广播系统关机事件
    app_publish_sys_event(EVENT_TYPE_SYS_SHUTDOWN);
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
    // 【新增】普通信号退出，发布系统关机事件
    app_publish_sys_event(EVENT_TYPE_SYS_SHUTDOWN);
    app_trigger_soft_exit();
}

static void _crash_signal_handler(int sig)
{
    (void)sig;
    const char *msg = "\n[Fatal] System crash, cleaning up...\n";
    (void)write(STDERR_FILENO, msg, strlen(msg));
    
    // 【新增】系统崩溃，发布致命错误事件（最高优先级系统通知）
    app_publish_sys_event(EVENT_TYPE_SYS_ERROR);
    
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
// 双总线初始化（事件+数据，核心底层）| 适配新API：按名称初始化，无句柄存储
// ==========================================================================
static int _main_init_buses(void)
{
    int ret = 0;

    LOG_I("Main: Initializing Event Bus[%s]...", SYS_EVENT_BUS_NAME);
    event_bus_config_t evt_bus_cfg = {0};
    evt_bus_cfg.name = SYS_EVENT_BUS_NAME;
    evt_bus_cfg.max_subscribers = CONFIG_EVENT_BUS_MAX_SUBSCRIBERS;
    ret = event_bus_init(&evt_bus_cfg);
    if (ret != 0) {
        LOG_E("Main: Failed to init Event Bus");
        return -1;
    }
    LOG_I("Main: Event Bus[%s] init success", SYS_EVENT_BUS_NAME);

    LOG_I("Main: Initializing Data Bus[%s]...", SYS_DATA_BUS_NAME);
    data_bus_config_t data_bus_cfg = {0};
    data_bus_cfg.name = SYS_DATA_BUS_NAME;
    data_bus_cfg.max_items = CONFIG_DATA_BUS_MAX_FRAMES;
    data_bus_cfg.max_item_size = 2 * 1024 * 1024;
    data_bus_cfg.max_subscribers = 16;
    ret = data_bus_init(&data_bus_cfg);
    if (ret != 0) {
        LOG_E("Main: Failed to init Data Bus");
        return -1;
    }
    LOG_I("Main: Data Bus[%s] init success", SYS_DATA_BUS_NAME);

    return 0;
}

// ==========================================================================
// 统一资源清理 | 适配新API：按名称销毁总线
// ==========================================================================
static void _cleanup_resources(void)
{
    LOG_I("Main: Starting resource cleanup...");

    app_restore_terminal_safe();
    
    // 优化：清理顺序与初始化严格反向（事件总线 → 数据总线）
    event_bus_deinit(SYS_EVENT_BUS_NAME);
    data_bus_deinit(SYS_DATA_BUS_NAME);
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

    // 【新增】V4.0强制：双总线初始化完成 → 发布核心就绪事件（依赖注入触发）
    app_publish_sys_event(EVENT_TYPE_SYS_CORE_READY);

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
    // 【新增】初始化失败 → 发布系统错误事件
    app_publish_sys_event(EVENT_TYPE_SYS_ERROR);
    
    LOG_E("Main: Application exited with error");
    _cleanup_resources();
    log_deinit();
    return -1;
}