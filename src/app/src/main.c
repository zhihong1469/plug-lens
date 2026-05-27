/* SPDX-License-Identifier: MIT */
/**
 * @file main.c
 * @brief 轻量化系统主入口（纯底层框架，无业务代码）
 * @details 职责：日志/信号/双总线/优雅退出/系统硬件(SD)初始化
 *          业务模块通过事件总线交互，main永久不变
 *          仅处理系统级异常,并通过事件总线发布通知,业务模块自行处理
 *          【边界约定】SD卡挂载/卸载 = 系统级硬件管理 | 图片存储 = 业务逻辑（main不处理）
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
#include "mem_adapter.h"  // 新增：TLSF内存适配层头文件
#include "daemon.h"  
// 引入新增组件头文件
#include "sd_mount.h"
#include "config_common.h"
#include "network_check.h"
#include "sys_time_sync.h"

#define MODULE_NAME               "MAIN"
#define MODULE_TAG                "[MAIN]"

// ==================================================================================
// 【框架约定】系统级总线固定名称（全局唯一，业务模块统一订阅）
// ==================================================================================
#define MAIN_EVENT_BUS_NAME    SYS_EVENT_BUS_NAME    // 系统事件总线名称

// ==================================================================================
// 【核心新增】TLSF静态内存池配置（嵌入式Linux 生产级大小）
// 大小规划：256MB = 32帧视频(128MB) + 事件总线/模块内存(64MB) + 冗余余量(64MB)
// 可根据硬件调整：128MB / 64MB
// ==================================================================================
#define MEM_POOL_SIZE         (40 * 1024 * 1024UL)  // x MB 静态内存池
static uint8_t s_mem_pool[MEM_POOL_SIZE] __attribute__((aligned(8))); // 8字节对齐，TLSF要求

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
    int ret = event_bus_publish_simple(MAIN_EVENT_BUS_NAME, type, "MAIN");
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

    LOG_I("Main: Initializing Event Bus[%s]...", MAIN_EVENT_BUS_NAME);
    event_bus_config_t evt_bus_cfg = {0};
    evt_bus_cfg.name = MAIN_EVENT_BUS_NAME;
    evt_bus_cfg.max_subscribers = CONFIG_EVENT_BUS_MAX_SUBSCRIBERS;
    ret = event_bus_init(&evt_bus_cfg);
    if (ret != 0) {
        LOG_E("Main: Failed to init Event Bus");
        return -1;
    }
    LOG_I("Main: Event Bus[%s] init success", MAIN_EVENT_BUS_NAME);

    return 0;
}

// ==========================================================================
// 统一资源清理（系统级安全退出）
// ==========================================================================
static void _cleanup_resources(void)
{
    LOG_I("Main: Starting resource cleanup...");

    // ===================== 系统级安全操作：磁盘同步 =====================
    // 作用：刷新所有文件系统缓存，防止数据损坏（工业级必需，非业务操作）
    sync();
    // ===================== 系统级硬件清理：SD卡安全卸载 =====================
    // 边界：仅卸载SD卡介质，业务层存储模块已通过SYS_SHUTDOWN事件自行清理
#if USE_SD
    SdMount_Deinit();
#endif
    app_restore_terminal_safe();
    
    // 优化：清理顺序与初始化严格反向（事件总线 → 数据总线）
    event_bus_deinit(MAIN_EVENT_BUS_NAME);
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

    // 1. 日志初始化（第一步，必须最先）
    log_init(LOG_LEVEL_INFO);

    // 系统级硬件初始化：SD卡自动挂载
#if USE_SD
    sd_state_t sd_state = SdMount_Init();
    if (sd_state == SD_MOUNTED) {
        LOG_I("Main: SD card mounted successfully");
    } else {
        LOG_W("Main: SD card mount failed, system running without external storage");
    }
#endif

    LOG_I("Main: ========================================");
    LOG_I("Main: Vision AI Framework Starting...");
    LOG_I("Main: ========================================");

// ==============================================
// 【模式切换】产品模式才开启守护进程
// ==============================================
#if RUN_PRODUCT_MODE
    LOG_I("Main: Creating daemon...");
    // ===================== 【修复3】调用顺序：先创建守护进程 =====================
    if (create_daemon() < 0) {
        LOG_E("Main: Failed to create daemon");
        return -1;
    }
    // ===================== 【修复4】后开启日志守护模式 =====================
    log_set_daemon_mode(1);  // 守护进程创建完成后，再设置仅写文件
#else
    LOG_I("Main: 调试模式 - 前台运行，支持键盘控制");
#endif

    // 2. 初始化TLSF静态内存池
    LOG_I("Main: Initializing TLSF static memory pool (Size: %zu MB)", MEM_POOL_SIZE / 1024 / 1024);
    mem_init(s_mem_pool, MEM_POOL_SIZE);
    LOG_I("Main: TLSF memory pool init success!");

// ===================== 网络 + 时间同步逻辑 =====================
#if USE_NET_CHECK
    LOG_I("Main: Start network status check...");

    bool eth_link = NetCheck_GetEth0LinkStatus();
    if (!eth_link)
    {
        LOG_W("Main: eth0 link down, no network cable connected");
    }

    bool internet_ok = NetCheck_GetInternetStatus();
    if (eth_link && !internet_ok)
    {
        LOG_W("Main: eth0 link up, but internet unreachable");
    }

#if USE_NET_TIME_SYNC
    if (internet_ok)
    {
        LOG_I("Main: Start system time sync...");

        bool tz_ok = TimeSync_SetCstTimezone();
        if (!tz_ok)
        {
            LOG_W("Main: Set timezone failed");
        }

        bool ntp_ok = TimeSync_NtpSync(NULL);
        if (ntp_ok)
        {
            LOG_I("Main: NTP time sync success");

            char time_buf[TIME_FORMAT_BUF_LEN] = {0};
            TimeSync_GetLocalTimeStr(time_buf, sizeof(time_buf));
            LOG_I("Main: Current system time: %s", time_buf);
        }
        else
        {
            LOG_W("Main: NTP time sync failed");
        }
    }
    else
    {
        LOG_W("Main: Skip time sync, network abnormal");
    }
#endif
#endif

    // 3. 系统底层初始化
    _init_signal_handling();
    if(app_exit_pipe_init() < 0) goto error_exit;
    app_set_terminal_noncanonical();

    // 4. 核心总线初始化
    if (_main_init_buses() != 0) goto error_exit;

    app_publish_sys_event(EVENT_TYPE_SYS_CORE_READY);

    // 5. 自动加载所有业务模块
    do_initcalls();

    // 6. 主循环
    LOG_I("Main: System running, waiting for exit signal...");
    while (g_app_ctx.app_running) {
        pause(); // 零CPU占用
    }

    // 正常退出
    LOG_I("Main: Application exited normally");
    _cleanup_resources();
    log_deinit();
    return 0;

error_exit:
    app_publish_sys_event(EVENT_TYPE_SYS_ERROR);
    LOG_E("Main: Application exited with error");
    _cleanup_resources();
    log_deinit();
    return -1;
}