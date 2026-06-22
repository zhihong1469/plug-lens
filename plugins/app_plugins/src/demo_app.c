/* SPDX-License-Identifier: MIT */
/**
 * @file    demo_app.c
 * @brief   Debug Mode Application Core Demo
 * @details Keyboard-interactive event-bus control demo,
 *          pure event-driven architecture, no direct API calls.
 *          Monitors system/capture/face service status,
 *          compiled ONLY in debug mode (RUN_PRODUCT_MODE=0).
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. Keyboard control for development & debugging.
 *          2. Full event subscription for status monitoring.
 *          3. Automatic resource cleanup on exit.
 */
#include "log.h"
#include "vision_ai_config.h"
#include "event_bus.h"
#include "initcall.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>

// ==========================================================================
// Module Configuration
// ==========================================================================
#define MODULE_NAME               "DEMO_APP"
#define MODULE_TAG                "[DEMO_APP]"
/** Global system event bus name */
#define APP_EVENT_BUS_NAME        SYS_EVENT_BUS_NAME
/** Main loop wait timeout (30ms) */
#define APP_LOOP_WAIT_US          30000
/** Keyboard debounce delay (50ms) */
#define KEY_DEBOUNCE_US           50000

// ==========================================================================
// Application Context Structure
// @details Compact 8-byte aligned structure for optimal memory usage
// ==========================================================================
typedef struct {
    int                     sub_sys;        // System event subscriber ID
    int                     sub_capture;    // Capture service subscriber ID
    int                     sub_face;       // Face service subscriber ID
    volatile bool           app_running;    // Global running flag
    volatile bool           is_paused;      // System pause state
    volatile bool           key_processing; // Keyboard debounce flag
    bool                    cap_ready;      // Capture service ready state
    bool                    face_ready;     // Face service ready state
} demo_app_t;

/** Static demo application context (global private) */
static demo_app_t s_demo;

// ==========================================================================
// Internal Function Declarations
// ==========================================================================
static void _demo_print_help(void);
static void _demo_handle_key(char cmd);

/**
 * @brief   System event callback handler
 * @param   event       Pointer to event structure
 * @param   user_data   User data (unused)
 * @return  None
 * @thread_safety Yes (volatile state access)
 */
static void _demo_sys_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    demo_app_t *srv = &s_demo;

    switch (event->type) {
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG " System core initialization completed");
            break;
        case EVENT_TYPE_SYS_PAUSE:
            if (!srv->is_paused) {
                LOG_I(MODULE_TAG " System paused");
                srv->is_paused = true;
            }
            break;
        case EVENT_TYPE_SYS_RESUME:
            if (srv->is_paused) {
                LOG_I(MODULE_TAG " System resumed");
                srv->is_paused = false;
            }
            break;
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            LOG_I(MODULE_TAG " Received exit event, safe shutdown");
            srv->app_running = false;
            break;
        case EVENT_TYPE_SYS_ERROR:
            LOG_E(MODULE_TAG " System error, forced exit");
            srv->app_running = false;
            break;
        default:
            break;
    }
}

/**
 * @brief   Capture service event callback handler
 * @param   event       Pointer to event structure
 * @param   user_data   User data (unused)
 * @return  None
 */
static void _demo_cap_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    demo_app_t *srv = &s_demo;

    if (event->type == EVENT_TYPE_CAPTURE_READY && !srv->cap_ready) {
        LOG_I(MODULE_TAG " Capture service ready");
        srv->cap_ready = true;
    }
    if (event->type == EVENT_TYPE_CAPTURE_STOPPED && srv->cap_ready) {
        LOG_I(MODULE_TAG " Capture service stopped");
        srv->cap_ready = false;
    }
}

/**
 * @brief   Face recognition service event callback handler
 * @param   event       Pointer to event structure
 * @param   user_data   User data (unused)
 * @return  None
 */
static void _demo_face_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    demo_app_t *srv = &s_demo;

    if (event->type == EVENT_TYPE_FACE_READY && !srv->face_ready) {
        LOG_I(MODULE_TAG " Face service ready");
        srv->face_ready = true;
    }
    if (event->type == EVENT_TYPE_FACE_STOPPED && srv->face_ready) {
        LOG_I(MODULE_TAG " Face service stopped");
        srv->face_ready = false;
    }
}

/**
 * @brief   Print debug mode keyboard help menu
 * @return  None
 */
static void _demo_print_help(void)
{
    LOG_I(MODULE_TAG " ========================================");
    LOG_I(MODULE_TAG "  Debug Mode Keyboard Control:");
    LOG_I(MODULE_TAG "    s - Start/Resume system");
    LOG_I(MODULE_TAG "    t - Pause system");
    LOG_I(MODULE_TAG "    q - Quit application");
    LOG_I(MODULE_TAG "    h - Show help menu");
    LOG_I(MODULE_TAG " ========================================");
}

/**
 * @brief   Keyboard command handler with debounce protection
 * @param   cmd     Input keyboard command character
 * @return  None
 * @note    Prevents repeated execution via debounce flag
 */
static void _demo_handle_key(char cmd)
{
    demo_app_t *srv = &s_demo;
    if (srv->key_processing) return;

    srv->key_processing = true;
    LOG_I(MODULE_TAG " Execute command: %c", cmd);

    switch (cmd) {
        case 's': case 'S':
            event_bus_publish_simple(APP_EVENT_BUS_NAME, EVENT_TYPE_SYS_RESUME, MODULE_NAME);
            break;
        case 't': case 'T':
            event_bus_publish_simple(APP_EVENT_BUS_NAME, EVENT_TYPE_SYS_PAUSE, MODULE_NAME);
            break;
        case 'q': case 'Q':
            event_bus_publish_simple(APP_EVENT_BUS_NAME, EVENT_TYPE_SYS_SHUTDOWN, MODULE_NAME);
            break;
        case 'h': case 'H':
            _demo_print_help();
            break;
        default:
            LOG_W(MODULE_TAG " Unknown command, press h for help");
            break;
    }

    usleep(KEY_DEBOUNCE_US);
    srv->key_processing = false;
}

/**
 * @brief   Demo application initialization
 * @details Initialize context, subscribe to all required events.
 * @return  0 on success, -1 on failure
 * @pre     Event bus must be initialized
 */
static int demo_app_init(void)
{
    demo_app_t *srv = &s_demo;
    memset(srv, 0, sizeof(demo_app_t));

    // Initialize default state
    srv->app_running = true;
    srv->is_paused = false;
    srv->key_processing = false;

    // Subscribe to system events
    event_subscriber_t sys_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _demo_sys_event_cb,
        .skip_self_published = true
    };
    srv->sub_sys = event_bus_subscribe_ex(APP_EVENT_BUS_NAME, &sys_sub, MODULE_NAME);

    // Subscribe to capture service events
    event_subscriber_t cap_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _demo_cap_event_cb,
        .skip_self_published = true
    };
    srv->sub_capture = event_bus_subscribe_ex(APP_EVENT_BUS_NAME, &cap_sub, MODULE_NAME);

    // Subscribe to face service events
    event_subscriber_t face_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _demo_face_event_cb,
        .skip_self_published = true
    };
    srv->sub_face = event_bus_subscribe_ex(APP_EVENT_BUS_NAME, &face_sub, MODULE_NAME);

    // Subscription validation
    if (srv->sub_sys <0 || srv->sub_capture <0 || srv->sub_face <0) {
        LOG_E(MODULE_TAG " Event subscription failed");
        return -1;
    }

    _demo_print_help();
    LOG_I(MODULE_TAG " Debug mode initialized, press s to start system");
    return 0;
}

/**
 * @brief   Debug mode main loop
 * @details Monitor stdin (keyboard) and event bus simultaneously.
 * @return  None
 * @thread_safety Yes
 */
static void demo_app_run(void)
{
    demo_app_t *srv = &s_demo;
    int bus_fd = event_bus_get_wait_fd(APP_EVENT_BUS_NAME);

    if (bus_fd < 0) {
        LOG_E(MODULE_TAG " Failed to get event bus file descriptor");
        return;
    }

    int max_fd = (bus_fd > STDIN_FILENO) ? bus_fd : STDIN_FILENO;
    LOG_I(MODULE_TAG " Debug mode running, press q to exit");

    while (srv->app_running) {
        fd_set read_fds;
        struct timeval tv = {0, APP_LOOP_WAIT_US};

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(bus_fd, &read_fds);

        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ret < 0 && errno == EINTR) continue;
        if (ret < 0) break;

        // Dispatch events from bus
        if (FD_ISSET(bus_fd, &read_fds)) {
            event_bus_dispatch(APP_EVENT_BUS_NAME);
        }

        // Handle keyboard input
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char cmd = 0;
            ssize_t n = read(STDIN_FILENO, &cmd, 1);
            if (n == 1 && cmd != '\n' && cmd != '\r') {
                _demo_handle_key(cmd);
            }
        }
    }

    LOG_I(MODULE_TAG " Main loop exited safely");
}

/**
 * @brief   Demo application de-initialization
 * @details Unsubscribe all events and clean up resources.
 * @return  None
 * @pre     Application must be stopping
 */
static void demo_app_deinit(void)
{
    demo_app_t *srv = &s_demo;
    event_bus_unsubscribe(APP_EVENT_BUS_NAME, srv->sub_face);
    event_bus_unsubscribe(APP_EVENT_BUS_NAME, srv->sub_capture);
    event_bus_unsubscribe(APP_EVENT_BUS_NAME, srv->sub_sys);
    LOG_I(MODULE_TAG " Resource cleanup completed");
}

/**
 * @brief   Demo application thread entry
 * @param   arg     Thread parameter (unused)
 * @return  NULL
 */
static void *_demo_thread(void *arg)
{
    (void)arg;
    demo_app_init();
    demo_app_run();
    demo_app_deinit();
    return NULL;
}

/**
 * @brief   Auto-initialization for debug demo
 * @details Create and detach normal priority thread.
 * @return  0 on success
 */
static int __demo_auto_init(void)
{
    pthread_t tid;
    pthread_create(&tid, NULL, _demo_thread, NULL);
    pthread_detach(tid);
    LOG_I(MODULE_TAG " Debug mode loaded successfully");
    return 0;
}

// Compile ONLY in debug mode
#if RUN_PRODUCT_MODE == 0
    // MODULE_INIT_LEVEL(INIT_APP, __demo_auto_init);
#endif