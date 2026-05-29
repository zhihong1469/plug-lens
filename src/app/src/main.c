/* SPDX-License-Identifier: MIT */
/**
 * @file    main.c
 * @brief   Lightweight Embedded Linux System Main Entry
 * @details Core framework implementation (No business code):
 *          - TLSF static memory pool management
 *          - Dual bus system initialization (Event Bus)
 *          - Signal handling & graceful exit pipeline
 *          - SD card hardware mounting/umounting
 *          - Network & system time synchronization
 *          - Daemon mode support (Product/Debug switch)
 *          - Terminal configuration & resource cleanup
 *          Business modules communicate via event bus, main remains permanent.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. Main only handles system-level resources, zero business logic.
 *          2. Strict initialization order: Log → Memory → Signal → Bus → Modules.
 *          3. Cleanup order is strictly reversed from initialization.
 *          4. SD management = system hardware; image storage = business logic.
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
#include "mem_adapter.h"
#include "daemon.h"
#include "sd_mount.h"
#include "config_common.h"
#include "network_check.h"
#include "sys_time_sync.h"

#define MODULE_NAME               "MAIN"
#define MODULE_TAG                "[MAIN]"

// ==================================================================================
// @brief   Fixed system event bus name (Global unique identifier)
// @note    All business modules subscribe to this bus for system notifications
// ==================================================================================
#define MAIN_EVENT_BUS_NAME    SYS_EVENT_BUS_NAME

// ==================================================================================
// @brief   TLSF static memory pool configuration (Embedded Linux production grade)
// @details 8-byte alignment required by TLSF allocator
//          Size: 40MB static pool for frames, buses and modules
// ==================================================================================
#define MEM_POOL_SIZE         (40 * 1024 * 1024UL)
static uint8_t s_mem_pool[MEM_POOL_SIZE] __attribute__((aligned(8)));

// ==================================================================================
// @brief   Global application context (System-level only)
// @details Minimal hidden context, no business variables or bus handles
// @note    Eliminates global variable abuse, fully encapsulated
// ==================================================================================
typedef struct {
    int                     exit_pipe[2];       /* Thread/signal safe exit pipeline */
    struct termios          old_termios;       /* Original terminal attributes */
    bool                    termios_saved;     /* Terminal config save flag */
    volatile bool           app_running;       /* Main loop running flag */
} app_context_t;

/** Global minimal system context (static private, hidden from external modules) */
static app_context_t g_app_ctx = {0};

// ==========================================================================
// @brief   Publish system-level event to global event bus
// @param   type    System event type to publish
// @return  None
// @note    Main only publishes system events, no business event handling
// ==========================================================================
static void app_publish_sys_event(event_type_t type)
{
    int ret = event_bus_publish_simple(MAIN_EVENT_BUS_NAME, type, "MAIN");
    if (ret != 0) {
        LOG_E("Main: Failed to publish sys event: %s", event_type_to_str(type));
    } else {
        LOG_I("Main: Published sys event: %s", event_type_to_str(type));
    }
}

// ==========================================================================
// @brief   Set terminal to non-canonical mode (Debug support)
// @return  None
// @note    Disables echo and canonical input for debug control
// ==========================================================================
static void app_set_terminal_noncanonical(void)
{
    struct termios new_termios;
    if (tcgetattr(STDIN_FILENO, &g_app_ctx.old_termios) == 0) {
        g_app_ctx.termios_saved = true;
        new_termios = g_app_ctx.old_termios;
        
        // Non-canonical mode: no line buffering, no echo
        new_termios.c_lflag &= ~(ICANON | ECHO); 
        new_termios.c_cc[VMIN] = 0;
        new_termios.c_cc[VTIME] = 0;
        
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
        LOG_I("Main: Terminal set to non-canonical mode");
    } else {
        LOG_W("Main: Failed to set terminal mode");
    }
}

// ==========================================================================
// @brief   Safely restore original terminal configuration
// @return  None
// @pre     Terminal attributes must be saved
// @post    Terminal reset to default state
// ==========================================================================
static void app_restore_terminal_safe(void)
{
    if (g_app_ctx.termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_app_ctx.old_termios);
        g_app_ctx.termios_saved = false;
    }
}

// ==========================================================================
// @brief   Initialize exit pipe for thread/signal safe shutdown
// @return  0 on success, -1 on failure
// @note    Core for industrial-grade graceful exit
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

// ==========================================================================
// @brief   Trigger soft system exit (Public API implementation)
// @return  None
// @post    app_running flag cleared, shutdown event published
// @thread_safety Yes
// ==========================================================================
void app_trigger_soft_exit(void)
{
    char sig = 'E';
    (void)write(g_app_ctx.exit_pipe[1], &sig, 1);
    g_app_ctx.app_running = false;
    app_publish_sys_event(EVENT_TYPE_SYS_SHUTDOWN);
}

// ==========================================================================
// @brief   Deinitialize exit pipe and release resources
// @return  None
// ==========================================================================
static void app_exit_pipe_deinit(void)
{
    close(g_app_ctx.exit_pipe[0]);
    close(g_app_ctx.exit_pipe[1]);
    LOG_I("Main: Global exit pipe deinit success");
}

// ==========================================================================
// @brief   Normal signal handler (SIGINT/SIGTERM)
// @param   sig     Received signal number
// @return  None
// ==========================================================================
static void _signal_handler(int sig)
{
    (void)sig;
    app_publish_sys_event(EVENT_TYPE_SYS_SHUTDOWN);
    app_trigger_soft_exit();
}

// ==========================================================================
// @brief   Fatal crash signal handler (SIGSEGV/SIGABRT)
// @param   sig     Received crash signal number
// @return  None
// @note    Immediate cleanup and forced exit on system crash
// ==========================================================================
static void _crash_signal_handler(int sig)
{
    (void)sig;
    const char *msg = "\n[Fatal] System crash, cleaning up...\n";
    (void)write(STDERR_FILENO, msg, strlen(msg));
    
    app_publish_sys_event(EVENT_TYPE_SYS_ERROR);
    app_trigger_soft_exit();
    usleep(100000); 
    _exit(1);
}

// ==========================================================================
// @brief   Initialize signal handling strategy (Debug/Product mode)
// @return  None
// @note    Signal init MUST be after daemon creation (fork inheritance)
// ==========================================================================
static void _init_signal_handling(void)
{
    struct sigaction sa;
    sa.sa_handler = _signal_handler;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);

#if !RUN_PRODUCT_MODE
    // Debug mode: Support Ctrl+C exit
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
#else
    // Product mode: Daemon, ignore terminal signals
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    sigaction(SIGTERM, &sa, NULL);
#endif

    // Crash handlers (enabled in all modes)
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
// @brief   Initialize core system buses (Event Bus)
// @return  0 on success, -1 on failure
// @note    No bus handles stored in main, accessed by name only
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
// @brief   Unified system resource cleanup (Industrial safe shutdown)
// @return  None
// @details Cleanup order reversed from initialization:
//          Sync disk → SD umount → Terminal → Bus → Exit pipe
// ==========================================================================
static void _cleanup_resources(void)
{
    LOG_I("Main: Starting resource cleanup...");

    // Flush filesystem cache (Prevent data corruption)
    sync();
    
    // Safe SD card umount (System hardware only)
#if USE_SD
    SdMount_Deinit();
#endif
    
    app_restore_terminal_safe();
    event_bus_deinit(MAIN_EVENT_BUS_NAME);
    app_exit_pipe_deinit();

    LOG_I("Main: Resource cleanup complete");
}

// ==========================================================================
// @brief   System main entry (Pure framework, zero business code)
// @param   argc    Argument count
// @param   argv    Argument list
// @return  0 on success, -1 on fatal error
// ==========================================================================
int main(int argc, char **argv)
{
    memset(&g_app_ctx, 0, sizeof(g_app_ctx));
    g_app_ctx.app_running = true;

    // Step 1: Initialize logging system (Mandatory first step)
    log_init(LOG_LEVEL_INFO);

    // Step 2: SD card hardware mount (System-level only)
#if USE_SD
    sd_state_t sd_state = SdMount_Init();
    if (sd_state == SD_MOUNTED) {
        LOG_I("Main: SD card mounted successfully");
    } else {
        LOG_W("Main: SD card mount failed, running without external storage");
    }
#endif

    LOG_I("Main: ========================================");
    LOG_I("Main: Vision AI Framework Starting...");
    LOG_I("Main: ========================================");

// Step 3: Daemon mode (Product mode only)
#if USE_SH == 0
    #if RUN_PRODUCT_MODE
        LOG_I("Main: Creating daemon...");
        if (create_daemon() < 0) {
            LOG_E("Main: Failed to create daemon");
            return -1;
        }
        log_set_daemon_mode(1);
    #else
        LOG_I("Main: Debug mode - Foreground running");
    #endif
#endif

    // Step 4: Initialize TLSF static memory pool
    LOG_I("Main: Initializing TLSF static memory pool (Size: %zu MB)", MEM_POOL_SIZE / 1024 / 1024);
    mem_init(s_mem_pool, MEM_POOL_SIZE);
    LOG_I("Main: TLSF memory pool init success!");

    // Step 5: Network & time synchronization
#if USE_NET_CHECK
    LOG_I("Main: Starting network status check...");
    bool eth_link = NetCheck_GetEth0LinkStatus();
    if (!eth_link) {
        LOG_W("Main: eth0 link down");
    }

    bool internet_ok = NetCheck_GetInternetStatus();
    if (eth_link && !internet_ok) {
        LOG_W("Main: No internet access");
    }

#if USE_NET_TIME_SYNC
    if (internet_ok) {
        LOG_I("Main: Synchronizing system time...");
        bool tz_ok = TimeSync_SetCstTimezone();
        if (!tz_ok) {
            LOG_W("Main: Timezone set failed");
        }

        bool ntp_ok = TimeSync_NtpSync(NULL);
        if (ntp_ok) {
            LOG_I("Main: NTP sync success");
            char time_buf[TIME_FORMAT_BUF_LEN] = {0};
            TimeSync_GetLocalTimeStr(time_buf, sizeof(time_buf));
            LOG_I("Main: Current time: %s", time_buf);
        } else {
            LOG_W("Main: NTP sync failed");
        }
    } else {
        LOG_W("Main: Network offline, skip time sync");
    }
#endif
#endif

    // Step 6: System low-level infrastructure
    _init_signal_handling();
    if(app_exit_pipe_init() < 0) goto error_exit;
    app_set_terminal_noncanonical();

    // Step 7: Core bus initialization
    if (_main_init_buses() != 0) goto error_exit;

    // Step 8: Notify system core ready
    app_publish_sys_event(EVENT_TYPE_SYS_CORE_READY);

    // Step 9: Auto-load all business modules
    do_initcalls();

    // Step 10: Main loop (Zero CPU usage)
    LOG_I("Main: System running, waiting for exit signal...");
    while (g_app_ctx.app_running) {
        pause();
    }

    // Normal shutdown
    LOG_I("Main: Application exited normally");
    _cleanup_resources();
    log_deinit();
    return 0;

// Fatal error handling
error_exit:
    app_publish_sys_event(EVENT_TYPE_SYS_ERROR);
    LOG_E("Main: Application exited with error");
    _cleanup_resources();
    log_deinit();
    return -1;
}