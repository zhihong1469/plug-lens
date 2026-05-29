/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           event_bus.h
 * @brief          High-Performance Asynchronous Event Bus for Embedded Linux [V2.0 Optimized]
 * @defgroup       EVENT_BUS
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
 * @author         luo
 * @date           2026
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
 * @note Reserved for extension, NORMAL used by default
 */
typedef enum {
    EVENT_PRIORITY_LOW       = 0,    /**< Low Priority */
    EVENT_PRIORITY_NORMAL    = 1,    /**< Normal Priority (Default) */
    EVENT_PRIORITY_HIGH      = 2,    /**< High Priority */
    EVENT_PRIORITY_CRITICAL  = 3,    /**< Critical Priority */
    EVENT_PRIORITY_MAX       = 4     /**< Max Priority */
} event_priority_t;

/**
 * @brief Event Core Structure
 * @details For small data notification between modules
 *          LARGE DATA MUST USE data_bus, NOT event bus
 */
typedef struct {
    uint64_t          timestamp;    /**< Timestamp (us) */
    void             *data;         /**< Extra data pointer */
    const char       *source;       /**< Publisher module name */
    uint32_t          data_len;     /**< Data length */
    event_type_t      type;         /**< Event type */
    event_priority_t  priority;     /**< Event priority */
} event_t;

/**
 * @brief Event Callback Function Type
 * @param event  Event pointer (managed by bus, NO free)
 * @param user_data Private data passed when subscribing
 */
typedef void (*event_callback_t)(const event_t *event, void *user_data);

/**
 * @brief Subscriber Configuration Structure
 */
typedef struct {
    event_callback_t  callback;          /**< Event trigger callback */
    void             *user_data;         /**< Callback private data */
    event_type_t      event_type;        /**< Target event type, INVALID=all */
    bool              skip_self_published;/**< Skip self-published: true=skip */
} event_subscriber_t;

/**
 * @brief Event Bus Initialization Configuration
 */
typedef struct {
    uint32_t    max_subscribers;  /**< Max supported subscribers */
    const char *name;             /**< Unique bus name (multi-instance) */
} event_bus_config_t;

// ==========================================================================
// Public Core APIs
// ==========================================================================

/**
 * @brief  Initialize event bus instance
 * @param  config Bus configuration (name + max subscribers)
 * @return 0=success, negative=failure
 * @note   Call once at system startup, support multi-instance
 */
int event_bus_init(const event_bus_config_t *config);

/**
 * @brief  Standard subscribe API (compatible with legacy code)
 * @param  name Bus name
 * @param  subscriber Subscriber configuration
 * @return Subscription ID (>0), negative=failure
 * @note   Default: Skip self-published events, subscriber ID=DEFAULT
 */
int event_bus_subscribe(const char *name, const event_subscriber_t *subscriber);

/**
 * @brief  Extended subscribe API (self-publish control)
 * @param  name Bus name
 * @param  subscriber Subscriber config
 * @param  subscriber_id Unique subscriber ID (recommend module name)
 * @return Subscription ID (>0), negative=failure
 * @note   Support custom self-filter for fault/self-check scenarios
 */
int event_bus_subscribe_ex(const char *name, const event_subscriber_t *subscriber, const char *subscriber_id);

/**
 * @brief  Unsubscribe event
 * @param  name Bus name
 * @param  subscription_id ID returned by subscribe
 * @return 0=success, negative=failure
 */
int event_bus_unsubscribe(const char *name, int subscription_id);

/**
 * @brief  Publish asynchronous event
 * @param  name Bus name
 * @param  event Event pointer (internal copy, external can free immediately)
 * @return 0=success, negative=failure
 * @note   Thread-safe, callable from any thread
 */
int event_bus_publish(const char *name, const event_t *event);

/**
 * @brief  Fast publish simple event (no extra data)
 * @param  name Bus name
 * @param  type Event type
 * @param  source Publisher ID (module name)
 * @return 0=success, negative=failure
 */
int event_bus_publish_simple(const char *name, event_type_t type, const char *source);

/**
 * @brief  Get event bus monitor FD (for select/poll)
 * @param  name Bus name
 * @return Pipe read FD, negative=failure
 */
int event_bus_get_wait_fd(const char *name);

/**
 * @brief  Dispatch events (MUST CALL IN MAIN THREAD)
 * @param  name Bus name
 * @return 0=success, negative=failure
 * @note   Run all subscriber callbacks in main thread
 */
int event_bus_dispatch(const char *name);

/**
 * @brief  Destroy event bus, release all resources
 * @param  name Bus name
 * @return 0=success, negative=failure
 */
int event_bus_deinit(const char *name);

/**
 * @brief  Convert event type to string (log print)
 * @param  type Event type enum
 * @return Event name string
 */
const char* event_type_to_str(event_type_t type);

/**
 * @brief  Get event publisher ID
 * @param  event Event pointer
 * @return Publisher module name string
 */
const char* event_get_source(const event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_H */