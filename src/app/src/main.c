/* SPDX-License-Identifier: MIT */
/**
 * @file    main.c
 * @brief   Lightweight system main entry (Pure low-level framework, no business code)
 * @details Responsibilities: Log/Signal/Dual-bus/Graceful exit/System hardware(SD) initialization
 *          Business modules interact via event bus, main remains unchanged permanently
 *          Only handle system-level exceptions and publish notifications via event bus,
 *          business modules handle processing independently
 *          @b Boundary Convention: SD mount/umount = System hardware management | Image storage = Business logic (Not handled in main)
 * @author  LuoZhihong
 * @date    2026-05-31
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
#include "mem_adapter.h"  // New: TLSF memory adapter layer header
#include "daemon.h"  
// New component headers
#include "sd_mount.h"
#include "config_common.h"
#include "network_check.h"
#include "sys_time_sync.h"

#define MODULE_NAME               "MAIN"
#define MODULE_TAG                "[MAIN]"

// ==================================================================================
// @brief Framework Convention: Fixed system-level bus names (Global unique, subscribed by all business modules)
// ==================================================================================
#define MAIN_EVENT_BUS_NAME    SYS_EVENT_BUS_NAME    // System event bus name

// ==================================================================================
// @brief Core New: TLSF static memory pool configuration (Embedded Linux production size)
//        Size Planning: 256MB = 32 Video Frames(128MB) + Bus/Module Memory(64MB) + Redundancy(64MB)
//        Adjustable based on hardware: 128MB / 64MB
// ==================================================================================
#define MEM_POOL_SIZE         (40 * 1024 * 1024UL)  // x MB Static memory pool
static uint8_t s_mem_pool[MEM_POOL_SIZE] __attribute__((aligned(8))); // 8-byte alignment, required by TLSF

// ==================================================================================
// @brief Global context: Only retain system low-level resources, NO business variables + NO bus handles (Removed)
//        Fully hidden externally, module internal APIs get handles, completely eliminate global variable abuse
// ==================================================================================
typedef struct {
    // System-level graceful exit pipe (Thread/Signal safe)
    int                     exit_pipe[2];

    // Terminal configuration (Debug use)
    struct termios          old_termios;
    bool                    termios_saved;

    // Main internal running flag
    volatile bool           app_running;
} app_context_t;

// Minimal global context (Static internal, fully hidden externally)
static app_context_t g_app_ctx = {0};

// ==========================================================================
// @brief New: Main private - System event unified publish interface (Pure low-level, no business logic)
//        Follow V4.0: Main only publishes system events, no perception of business modules | Adapt new bus: Publish by name
// ==========================================================================
static void app_publish_sys_event(event_type_t type)
{
    // Call directly by bus name, main holds NO handles
    int ret = event_bus_publish_simple(MAIN_EVENT_BUS_NAME, type, "MAIN");
    if (ret != 0) {
        LOG_E("Main: Failed to publish sys event: %s", event_type_to_str(type));
    } else {
        LOG_I("Main: Published sys event: %s", event_type_to_str(type));
    }
}

// ==========================================================================
// @brief Terminal low-level infrastructure (Internal use)
// ==========================================================================
static void app_set_terminal_noncanonical(void)
{
    struct termios new_termios;
    if (tcgetattr(STDIN_FILENO, &g_app_ctx.old_termios) == 0) {
        g_app_ctx.termios_saved = true;
        new_termios = g_app_ctx.old_termios;
        
        // Bug fix: Standard non-canonical mode config, remove duplicate assignment
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
// @brief Exit pipe low-level infrastructure (System-level safe exit)
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
    
    // New: Broadcast system shutdown event on soft exit
    app_publish_sys_event(EVENT_TYPE_SYS_SHUTDOWN);
}

static void app_exit_pipe_deinit(void)
{
    close(g_app_ctx.exit_pipe[0]);
    close(g_app_ctx.exit_pipe[1]);
    LOG_I("Main: Global exit pipe deinit success");
}

// ==========================================================================
// @brief Signal handling (Ctrl+C / Crash safe processing)
// ==========================================================================
static void _signal_handler(int sig)
{
    (void)sig;
    // New: Normal signal exit, publish system shutdown event
    app_publish_sys_event(EVENT_TYPE_SYS_SHUTDOWN);
    app_trigger_soft_exit();
}

static void _crash_signal_handler(int sig)
{
    (void)sig;
    const char *msg = "\n[Fatal] System crash, cleaning up...\n";
    (void)write(STDERR_FILENO, msg, strlen(msg));
    
    // New: System crash, publish fatal error event (Highest priority system notification)
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

    // ==============================================
    // Mode 1: Debug Mode (Foreground running)
    // Respond to Ctrl+C + System shutdown, ignore network disconnect signals
    // ==============================================
#if !RUN_PRODUCT_MODE
    sigaction(SIGINT, &sa, NULL);   // Support Ctrl+C exit
    sigaction(SIGTERM, &sa, NULL);  // Support system shutdown graceful exit

    // Mandatory: Ignore network disconnect signal, prevent crash on RTSP client disconnect
    signal(SIGPIPE, SIG_IGN);

// ==============================================
// Mode 2: Product Mode (Daemon/Background running)
// Ignore all terminal signals, only respond to system shutdown and crash
// ==============================================
#else
    // Core fix: Ignore terminal hangup signal (Permanently fix background kill issue)
    signal(SIGHUP, SIG_IGN);
    
    // Ignore Ctrl+C (Daemon has no terminal, never receives)
    signal(SIGINT, SIG_IGN);
    
    // Ignore network disconnect signal
    signal(SIGPIPE, SIG_IGN);
    
    // Only retain system shutdown signal (Graceful exit on reboot/halt)
    sigaction(SIGTERM, &sa, NULL);
#endif

    // Crash signals: Must be handled in both modes
    struct sigaction sa_crash;
    sa_crash.sa_handler = _crash_signal_handler;
    sa_crash.sa_flags = 0;
    sigfillset(&sa_crash.sa_mask);
    sigaction(SIGABRT, &sa_crash, NULL);
    sigaction(SIGSEGV, &sa_crash, NULL);
    
    LOG_I("Main: Signal handler init success (mode: %s)", 
          RUN_PRODUCT_MODE ? "PRODUCT" : "DEBUG");
}

// ==========================================================================
// @brief Dual bus initialization (Event + Data, Core low-level)
//        Adapt new API: Initialize by name, NO handle storage
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
// @brief Unified resource cleanup (System-level safe exit)
// ==========================================================================
static void _cleanup_resources(void)
{
    LOG_I("Main: Starting resource cleanup...");

    // ===================== System-level safe operation: Disk sync =====================
    // Function: Flush all filesystem cache, prevent data corruption (Industrial mandatory, non-business operation)
    sync();
    // ===================== System-level hardware cleanup: SD card safe umount =====================
    // Boundary: Only unmount SD media, business storage modules have self-cleaned via SYS_SHUTDOWN event
#if USE_SD
    SdMount_Deinit();
#endif
    app_restore_terminal_safe();
    
    // Optimization: Cleanup order strictly reversed from initialization (Event bus → Data bus)
    event_bus_deinit(MAIN_EVENT_BUS_NAME);
    app_exit_pipe_deinit();

    LOG_I("Main: Resource cleanup complete");
}

// ==========================================================================
// @brief Main function: Pure low-level framework, ZERO business code
// ==========================================================================
int main(int argc, char **argv)
{
    memset(&g_app_ctx, 0, sizeof(g_app_ctx));
    g_app_ctx.app_running = true;

    // 1. Log initialization (First step, mandatory)
    log_init(LOG_LEVEL_INFO);

    // System-level hardware initialization: SD auto mount
#if USE_SD
    sd_state_t sd_state = SdMount_Init();
    if (sd_state == SD_MOUNTED) {
        LOG_I("Main: SD card mounted successfully");
    } else {
        LOG_W("Main: The internal SD card mounting of the program failed, and the system ran without external storage");
    }
#endif

    LOG_I("Main: ========================================");
    LOG_I("Main: Vision AI Framework Starting...");
    LOG_I("Main: ========================================");

// ==============================================
// Mode Switch: Enable daemon only in product mode
// ==============================================
#if USE_SH == 0
    #if RUN_PRODUCT_MODE
        LOG_I("Main: Creating daemon...");
        // ===================== Fix 3: Call order: Create daemon first =====================
        if (create_daemon() < 0) {
            LOG_E("Main: Failed to create daemon");
            return -1;
        }
        // ===================== Fix 4: Enable log daemon mode after =====================
        log_set_daemon_mode(1);  // Set file-only log after daemon creation
    #else
        LOG_I("Main: Debug mode - Foreground running, keyboard control supported");
    #endif
#endif

    // 2. Initialize TLSF static memory pool
    LOG_I("Main: Initializing TLSF static memory pool (Size: %zu MB)", MEM_POOL_SIZE / 1024 / 1024);
    mem_init(s_mem_pool, MEM_POOL_SIZE);
    LOG_I("Main: TLSF memory pool init success!");

// ===================== Network + Time Sync Logic =====================
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

    // 3. System low-level initialization
    // Important: Signal initialization MUST be after daemon creation
    // Reason: fork() inherits parent signal handling, daemon needs independent signal strategy
    _init_signal_handling();
    if(app_exit_pipe_init() < 0) goto error_exit;
    app_set_terminal_noncanonical();

    // 4. Core bus initialization
    if (_main_init_buses() != 0) goto error_exit;

    app_publish_sys_event(EVENT_TYPE_SYS_CORE_READY);

    // 5. Auto-load all business modules
    do_initcalls();

    // 6. Main loop
    LOG_I("Main: System running, waiting for exit signal...");
    while (g_app_ctx.app_running) {
        pause(); // Zero CPU usage
    }

    // Normal exit
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