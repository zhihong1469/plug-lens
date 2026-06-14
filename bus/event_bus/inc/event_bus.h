/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           event_bus.h
 * @brief          High-Performance Asynchronous Event Bus for Embedded Linux [V2.0 Optimized]
 * @defgroup       EVENT_BUS
 * @author         LuoZhihong
 * @date           2026
 * @version        V2.0
 *
 * @details
 * 【Core Features】
 *  1. Pure Asynchronous Event-Driven: Queue cache + pipe wakeup, non-blocking, no infinite loop
 *  2. Pub-Sub Mode: Multi-instance, multi-subscriber, event type filtering
 *  3. Self-Publish Filter: Configurable to skip self-published events, avoid logic conflicts
 *  4. Main Thread Dispatch: All callbacks run in main thread, eliminate concurrency/ deadlock
 *  5. Thread-Safe: Multi-thread publish/subscribe without race risk
 *
 * 【V2.0 Key Optimizations】
 *  1. Memory Management: TLSF static pool + native malloc dual mode, zero fragmentation
 *  2. Lock Granularity: Split global lock into sub/queue lock, greatly improve concurrency
 *  3. Atomic Operations: C11 atomic variables for event stats, lock-free high performance
 *  4. Security: Magic number verification, block illegal pointers, no crash
 *  5. Memory Managed: Auto memory management, forbid external alloc/free
 *
 * 【Usage Rules · MUST Follow】
 *  1. Init Order: Initialize mem_adapter FIRST, then event bus
 *  2. Thread Rule: event_bus_dispatch() MUST be called in MAIN THREAD
 *  3. Memory Rule: Event struct copied/freed by bus, no external management
 *  4. Concurrency Rule: All APIs thread-safe, no external lock needed
 *  5. Handle Rule: Sub ID/Bus name as unique ID, no internal structure tampering
 *
 * @example
 *  // ========== 1. Initialization (System Startup) ==========
 *  event_bus_config_t config = {
 *      .name = "main_bus",
 *      .max_subscribers = 16
 *  };
 *  event_bus_init(&config);
 *
 *  // ========== 2. Subscribe Event (Module Init) ==========
 *  void my_event_callback(const event_t *event, void *user_data) {
 *      printf("Event: %s, Source: %s\n",
 *             event_type_to_str(event->type), event_get_source(event));
 *  }
 *
 *  event_subscriber_t sub = {
 *      .callback = my_event_callback,
 *      .event_type = EVENT_TYPE_SYS_CORE_READY,
 *      .skip_self_published = true
 *  };
 *  int sub_id = event_bus_subscribe("main_bus", &sub);
 *
 *  // ========== 3. Publish Event (Any Thread) ==========
 *  // Simple Event (No Data)
 *  event_bus_publish_simple("main_bus", EVENT_TYPE_SYS_CORE_READY, "SYSTEM");
 *
 *  // Custom Event (With Data)
 *  event_t evt = {0};
 *  evt.type = EVENT_TYPE_MOD_INIT_DONE;
 *  evt.source = "CAPTURE";
 *  evt.data = &my_data;
 *  evt.data_len = sizeof(my_data);
 *  event_bus_publish("main_bus", &evt);
 *
 *  // ========== 4. Main Thread Dispatch (Core Loop) ==========
 *  int fd = event_bus_get_wait_fd("main_bus");
 *  while(1) {
 *      // select/poll monitor fd
 *      event_bus_dispatch("main_bus");
 *  }
 *
 *  // ========== 5. Unsubscribe (Module Exit) ==========
 *  event_bus_unsubscribe("main_bus", sub_id);
 *
 *  // ========== 6. Deinitialize (System Shutdown) ==========
 *  event_bus_deinit("main_bus");
 *
 ******************************************************************************
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "vision_ai_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// Public Type Definitions
// ==========================================================================

/**
 * @brief Event Priority Enumeration
 * @note Reserved for extension, NORMAL priority used by default
 */
typedef enum {
    EVENT_PRIORITY_LOW       = 0,    /**< Low Priority */
    EVENT_PRIORITY_NORMAL    = 1,    /**< Normal Priority (Default) */
    EVENT_PRIORITY_HIGH      = 2,    /**< High Priority */
    EVENT_PRIORITY_CRITICAL  = 3,    /**< Critical Priority */
    EVENT_PRIORITY_MAX       = 4     /**< Max Priority (Boundary Check) */
} event_priority_t;

/**
 * @brief Event Core Structure
 * @details Lightweight event for inter-module communication
 * @warning LARGE DATA TRANSFER MUST USE data_bus, NOT event bus
 */
typedef struct {
    uint64_t          timestamp;    /**< Event timestamp (microseconds) */
    void             *data;         /**< Pointer to event payload data */
    const char       *source;       /**< Event publisher module name */
    uint32_t          data_len;     /**< Length of payload data (bytes) */
    event_type_t      type;         /**< Event type ID */
    event_priority_t  priority;     /**< Event priority level */
} event_t;

/**
 * @brief Event Callback Function Prototype
 * @param event      Pointer to event (managed by bus, DO NOT FREE)
 * @param user_data  Private data passed during subscription
 */
typedef void (*event_callback_t)(const event_t *event, void *user_data);

/**
 * @brief Event Subscriber Configuration Structure
 */
typedef struct {
    event_callback_t  callback;          /**< Event trigger callback function */
    void             *user_data;         /**< Private data for callback */
    event_type_t      event_type;        /**< Target event type (INVALID = all events) */
    bool              skip_self_published;/**< Skip self-published events flag */
} event_subscriber_t;

/**
 * @brief Event Bus Initialization Configuration
 */
typedef struct {
    uint32_t    max_subscribers;  /**< Maximum supported subscribers per bus */
    const char *name;             /**< Unique bus name (multi-instance support) */
} event_bus_config_t;

// ==========================================================================
// Public Core APIs
// ==========================================================================

/**
 * @brief  Initialize event bus instance
 * @param  config  Pointer to bus configuration struct
 * @return 0=success, negative value=failure
 * @note   Call once at system startup; support multi-instance
 * @thread_safe
 */
int event_bus_init(const event_bus_config_t *config);

/**
 * @brief  Standard event subscription (compatible with legacy code)
 * @param  name        Bus instance name
 * @param  subscriber  Pointer to subscriber configuration
 * @return Positive subscription ID=success, negative value=failure
 * @note   Default subscriber ID = "DEFAULT", self-filter configurable
 * @thread_safe
 */
int event_bus_subscribe(const char *name, const event_subscriber_t *subscriber);

/**
 * @brief  Extended subscription with custom subscriber ID
 * @param  name            Bus instance name
 * @param  subscriber      Pointer to subscriber configuration
 * @param  subscriber_id   Unique subscriber ID (recommend module name)
 * @return Positive subscription ID=success, negative value=failure
 * @note   Support precise self-published event filtering
 * @thread_safe
 */
int event_bus_subscribe_ex(const char *name, const event_subscriber_t *subscriber, const char *subscriber_id);

/**
 * @brief  Unsubscribe from event notifications
 * @param  name              Bus instance name
 * @param  subscription_id   ID returned by subscribe API
 * @return 0=success, negative value=failure
 * @thread_safe
 */
int event_bus_unsubscribe(const char *name, int subscription_id);

/**
 * @brief  Publish asynchronous event to bus
 * @param  name   Bus instance name
 * @param  event  Pointer to event (internal copied, external can free immediately)
 * @return 0=success, negative value=failure
 * @note   Callable from any thread; thread-safe
 * @thread_safe
 */
int event_bus_publish(const char *name, const event_t *event);

/**
 * @brief  Fast publish simple event (no payload data)
 * @param  name    Bus instance name
 * @param  type    Event type ID
 * @param  source  Publisher module name
 * @return 0=success, negative value=failure
 * @thread_safe
 */
int event_bus_publish_simple(const char *name, event_type_t type, const char *source);

/**
 * @brief  Get event bus wait FD for select/poll
 * @param  name  Bus instance name
 * @return Pipe read FD=success, negative value=failure
 * @thread_safe
 */
int event_bus_get_wait_fd(const char *name);

/**
 * @brief  Dispatch queued events to subscribers
 * @param  name  Bus instance name
 * @return 0=success, negative value=failure
 * @warning  MUST BE CALLED IN MAIN THREAD ONLY
 * @note     All callbacks run in main thread to avoid deadlock
 */
int event_bus_dispatch(const char *name);

/**
 * @brief  Deinitialize event bus and release all resources
 * @param  name  Bus instance name
 * @return 0=success, negative value=failure
 * @thread_safe
 */
int event_bus_deinit(const char *name);

/**
 * @brief  Convert event type to human-readable string (for log)
 * @param  type  Event type ID
 * @return Event name string
 * @thread_safe
 */
const char* event_type_to_str(event_type_t type);

/**
 * @brief  Get event publisher module name
 * @param  event  Pointer to event struct
 * @return Publisher name string
 * @thread_safe
 */
const char* event_get_source(const event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_H */