/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           global_fsm.h
 * @brief          Global Master Finite State Machine for Embedded Linux System
 * @defgroup       GLOBAL_FSM
 * @details
 * 【Global FSM Iron Rules】
 *  1. Only aggregate global status & make decisions, never interfere sub-module business
 *  2. Only depend on module_fsm base class, no dependency on specific services/buses
 *  3. All status changes notified via callbacks, reserved interface for Event Bus
 *  4. Thread-safe design, support multi-thread event post & module register
 *
 * 【Core Features】
 *  1. Multi-module management: Register critical/non-critical sub-modules
 *  2. Automatic global status decision: Based on all sub-module states
 *  3. Event-driven: System start/stop/shutdown controlled by global events
 *  4. Callback notification: Reserved for Event Bus integration
 *  5. Thread-safe: Mutex protected all critical operations
 *
 * @example
 *  // ========== 1. Initialize Global FSM ==========
 *  void on_global_state_changed(global_state_t old_s, global_state_t new_s, void *data) {
 *      // Publish event to Event Bus here
 *      LOG_I("Global State: %s -> %s", global_state_to_str(old_s), global_state_to_str(new_s));
 *  }
 * 
 *  global_fsm_config_t cfg = {
 *      .max_modules = 8,
 *      .state_cb = on_global_state_changed,
 *      .event_cb = NULL,  // Bind to Event Bus if needed
 *      .user_data = NULL
 *  };
 *  global_fsm_handle_t g_fsm;
 *  global_fsm_init(&cfg, &g_fsm);
 * 
 *  // ========== 2. Register Sub-Module ==========
 *  module_fsm_handle_t capture_fsm;
 *  module_fsm_init(&capture_fsm, "CAPTURE");
 *  // Bind module callback to Global FSM
 *  module_fsm_set_state_cb(capture_fsm, global_fsm_on_module_state_change, g_fsm);
 *  // Register to Global FSM (critical module)
 *  global_fsm_register_module(g_fsm, "CAPTURE", capture_fsm, true);
 * 
 *  // ========== 3. Post Global Event ==========
 *  global_fsm_post_event(g_fsm, GLOBAL_EVENT_SYSTEM_START);
 * 
 *  // ========== 4. Get Current Global State ==========
 *  global_state_t state = global_fsm_get_state(g_fsm);
 * 
 *  // ========== 5. Deinitialize ==========
 *  global_fsm_deinit(g_fsm);
 * 
 * @author         luo
 * @date           2026
 ******************************************************************************
 */
#ifndef GLOBAL_FSM_H
#define GLOBAL_FSM_H

#include "module_fsm.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// Opaque Handle (Fully encapsulated internal implementation)
// ==========================================================================
typedef void* global_fsm_handle_t;

// ==========================================================================
// Global System States (System-level lifecycle)
// ==========================================================================
typedef enum {
    GLOBAL_STATE_INVALID = 0,
    GLOBAL_STATE_INIT,           // System initializing
    GLOBAL_STATE_READY,          // All sub-modules ready
    GLOBAL_STATE_RUNNING,        // All sub-modules running
    GLOBAL_STATE_DEGRADED,       // Partial non-critical modules error
    GLOBAL_STATE_ERROR,          // Critical modules error
    GLOBAL_STATE_SHUTTING_DOWN,  // System shutting down
    GLOBAL_STATE_OFF,            // System powered off
    GLOBAL_STATE_MAX
} global_state_t;

// ==========================================================================
// Global Events (Drive global state transition)
// ==========================================================================
typedef enum {
    GLOBAL_EVENT_INVALID = 0,
    GLOBAL_EVENT_MODULE_READY,       // Sub-module ready
    GLOBAL_EVENT_MODULE_RUNNING,      // Sub-module running
    GLOBAL_EVENT_MODULE_ERROR,        // Sub-module error
    GLOBAL_EVENT_MODULE_STOPPED,      // Sub-module stopped
    GLOBAL_EVENT_SYSTEM_START,        // System start command
    GLOBAL_EVENT_SYSTEM_STOP,         // System stop command
    GLOBAL_EVENT_SYSTEM_SHUTDOWN,     // System shutdown command
    GLOBAL_EVENT_MAX
} global_event_t;

// ==========================================================================
// Callback Definitions (Reserved for Event Bus Integration)
// ==========================================================================
typedef void (*global_state_change_cb_t)(global_state_t old_state,
                                          global_state_t new_state,
                                          void *user_data);

typedef void (*global_event_notify_cb_t)(global_event_t event,
                                          const char *module_name,
                                          void *user_data);

// ==========================================================================
// Global FSM Configuration
// ==========================================================================
typedef struct {
    uint32_t max_modules;               // Max supported sub-modules
    global_state_change_cb_t state_cb;  // Global state change callback
    global_event_notify_cb_t event_cb;  // Global event notify callback
    void *user_data;                     // User private data
} global_fsm_config_t;

// ==========================================================================
// Public Core APIs
// ==========================================================================

/**
 * @brief  Initialize global FSM
 * @param  config     Global FSM configuration
 * @param  out_handle Output global FSM handle
 * @return 0=success, negative=failure
 */
int global_fsm_init(const global_fsm_config_t *config,
                    global_fsm_handle_t *out_handle);

/**
 * @brief  Register sub-module to global FSM
 * @param  handle       Global FSM handle
 * @param  module_name  Unique sub-module name
 * @param  module_fsm   Sub-module FSM handle
 * @param  is_critical  True=critical module (error → global error)
 * @return 0=success, negative=failure
 */
int global_fsm_register_module(global_fsm_handle_t handle,
                               const char *module_name,
                               module_fsm_handle_t module_fsm,
                               bool is_critical);

/**
 * @brief  Post global event to drive state transition
 * @param  handle  Global FSM handle
 * @param  event   Global event type
 * @return 0=success, negative=failure
 */
int global_fsm_post_event(global_fsm_handle_t handle, global_event_t event);

/**
 * @brief  Get current global system state
 * @param  handle  Global FSM handle
 * @return Current global state
 */
global_state_t global_fsm_get_state(global_fsm_handle_t handle);

/**
 * @brief  Deinitialize global FSM, release all resources
 * @param  handle  Global FSM handle
 * @return 0=success, negative=failure
 */
int global_fsm_deinit(global_fsm_handle_t handle);

/**
 * @brief  Convert global state to string (for log)
 * @param  state  Global state enum
 * @return State name string
 */
const char* global_state_to_str(global_state_t state);

/**
 * @brief  Convert global event to string (for log)
 * @param  event  Global event enum
 * @return Event name string
 */
const char* global_event_to_str(global_event_t event);

/**
 * @brief  Standard sub-module state change callback
 * @note   MUST be bound to sub-module FSM state callback
 * @param  module_name Sub-module name
 * @param  old_state   Sub-module old state
 * @param  new_state   Sub-module new state
 * @param  user_data   Global FSM handle
 */
void global_fsm_on_module_state_change(const char *module_name,
                                        module_state_t old_state,
                                        module_state_t new_state,
                                        void *user_data);

#endif /* GLOBAL_FSM_H */