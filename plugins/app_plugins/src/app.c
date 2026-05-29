/* SPDX-License-Identifier: MIT */
/**
 * @file    app.c
 * @brief   Product Version Application Layer
 * @details Pure event-bus decoupled architecture, NO direct module API calls.
 *          Core responsibilities: System event subscription, automatic startup,
 *          unattended operation, event loop maintenance, graceful shutdown.
 *          Compiled ONLY in product mode (RUN_PRODUCT_MODE=1).
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. Full decoupling via system event bus, no hard dependencies.
 *          2. Real-time priority thread for stable unattended operation.
 *          3. Only respond to system-level events (shutdown/error/stop).
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

#define MODULE_NAME        "APP"
#define MODULE_TAG         "[APP]"
/** System global event bus name */
#define APP_EVENT_BUS      SYS_EVENT_BUS_NAME
/** Wait time for low-level module initialization (300ms) */
#define WAIT_MODULE_READY  300000

/** Global running flag for application main loop */
static volatile bool g_app_running = true;

/**
 * @brief   System event callback handler
 * @details Listen and handle system shutdown/stop/error events.
 * @param   event   Pointer to system event structure
 * @param   data    User-defined callback data (unused)
 * @return  None
 * @thread_safety Yes (volatile flag access)
 */
static void _app_sys_cb(const event_t *event, void *data) {
    (void)data;
    if (event->type == EVENT_TYPE_SYS_SHUTDOWN ||
        event->type == EVENT_TYPE_SYS_STOP ||
        event->type == EVENT_TYPE_SYS_ERROR) {
        LOG_I(MODULE_TAG " Received exit signal, executing safe shutdown");
        g_app_running = false;
    }
}

/**
 * @brief   Automatic system startup via event bus
 * @details Wait for low-level initialization, publish RESUME event
 *          and force dispatch to trigger all services.
 * @return  None
 * @pre     Event bus must be initialized successfully
 * @post    System resume event published and dispatched
 */
static void _app_auto_start(void) {
    LOG_I(MODULE_TAG " Waiting for low-level module initialization...");
    usleep(WAIT_MODULE_READY);

    LOG_I(MODULE_TAG " Sending system startup command: RESUME");
    // Publish startup event to trigger all services
    event_bus_publish_simple(APP_EVENT_BUS, EVENT_TYPE_SYS_RESUME, MODULE_NAME);
    // Force immediate event dispatch
    event_bus_dispatch(APP_EVENT_BUS);

    LOG_I(MODULE_TAG " Startup command dispatched to all services successfully");
}

/**
 * @brief   Product mode main loop (unattended operation)
 * @details Use select() to wait for event bus signals, zero CPU occupancy.
 * @return  None
 * @pre     Event bus file descriptor must be valid
 * @thread_safety Yes
 */
static void app_run(void) {
    int bus_fd = event_bus_get_wait_fd(APP_EVENT_BUS);
    if (bus_fd < 0) {
        LOG_E(MODULE_TAG " Failed to get event bus file descriptor");
        return;
    }

    LOG_I(MODULE_TAG " Product mode running, unattended automatic operation");
    while (g_app_running) {
        fd_set fds;
        struct timeval tv = {0, 30000}; // 30ms timeout
        FD_ZERO(&fds);
        FD_SET(bus_fd, &fds);

        int ret = select(bus_fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(bus_fd, &fds)) {
            event_bus_dispatch(APP_EVENT_BUS);
        }
    }
}

/**
 * @brief   Application initialization
 * @details Subscribe to all system-level events.
 * @return  0 on success, negative on failure
 * @pre     Event bus must be initialized
 */
static int app_init(void) {
    event_subscriber_t sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _app_sys_cb,
        .skip_self_published = false
    };
    event_bus_subscribe_ex(APP_EVENT_BUS, &sub, MODULE_NAME);
    LOG_I(MODULE_TAG " Product APP initialization completed");
    return 0;
}

/**
 * @brief   Application thread entry
 * @details Initialize, auto-start and run main loop.
 * @param   arg     Thread input parameter (unused)
 * @return  NULL
 * @thread_safety Yes
 */
static void *app_thread(void *arg) {
    (void)arg;
    app_init();
    _app_auto_start();
    app_run();
    return NULL;
}

/**
 * @brief   Auto-initialization for application module
 * @details Create real-time priority thread (SCHED_FIFO, priority 60).
 * @return  0 on success
 * @note    Real-time priority ensures stable event processing
 */
static int __app_auto_init(void) {
    pthread_t tid;
    pthread_attr_t thread_attr;
    struct sched_param sched_param;

    // Initialize thread attributes
    pthread_attr_init(&thread_attr);
    // Set real-time scheduling policy
    pthread_attr_setschedpolicy(&thread_attr, SCHED_FIFO);
    sched_param.sched_priority = 60;
    pthread_attr_setschedparam(&thread_attr, &sched_param);
    pthread_attr_setinheritsched(&thread_attr, PTHREAD_EXPLICIT_SCHED);

    // Create and detach thread
    pthread_create(&tid, &thread_attr, app_thread, NULL);
    pthread_detach(tid);

    // Cleanup thread attributes
    pthread_attr_destroy(&thread_attr);
    return 0;
}

// Compile ONLY in product mode
#if RUN_PRODUCT_MODE
    MODULE_INIT_LEVEL(INIT_APP, __app_auto_init);
#endif