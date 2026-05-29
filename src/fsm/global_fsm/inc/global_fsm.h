/**
 * @file    global_fsm.h
 * @brief   Global Master Finite State Machine for Embedded Linux System
 *  
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All public APIs are thread-safe.
 *          2. Call sequence: init → register modules → post events → get state → deinit.
 *          3. Bind global_fsm_on_module_state_change() to all sub-module FSMs.
 *          4. Critical module errors trigger global ERROR state immediately.
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
 */
#ifndef GLOBAL_FSM_H
#define GLOBAL_FSM_H

#include "module_fsm.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// Opaque Handle (Fully encapsulated internal implementation)
// ==========================================================================
/**
 * @brief   Opaque handle for Global FSM instance
 * @details External modules only use this pointer, internal structure is hidden
 */
typedef void* global_fsm_handle_t;

// ==========================================================================
// Global System States (System-level lifecycle)
// ==========================================================================
/**
 * @brief   Enumeration of global system lifecycle states
 * @details Aggregated state of all registered sub-modules
 */
typedef enum {
    GLOBAL_STATE_INVALID = 0,        /**< Invalid/undefined global state */
    GLOBAL_STATE_INIT,               /**< System initialization in progress */
    GLOBAL_STATE_READY,              /**< All sub-modules ready for operation */
    GLOBAL_STATE_RUNNING,            /**< All sub-modules operating normally */
    GLOBAL_STATE_DEGRADED,           /**< Non-critical modules failed, system partial running */
    GLOBAL_STATE_ERROR,              /**< Critical modules failed, system abnormal */
    GLOBAL_STATE_SHUTTING_DOWN,     /**< System shutdown in progress */
    GLOBAL_STATE_OFF,                /**< System powered off */
    GLOBAL_STATE_MAX                 /**< Boundary for state validation */
} global_state_t;

// ==========================================================================
// Global Events (Drive global state transition)
// ==========================================================================
/**
 * @brief   Enumeration of global system trigger events
 * @details Control system-level behavior and state transitions
 */
typedef enum {
    GLOBAL_EVENT_INVALID = 0,        /**< Invalid/undefined global event */
    GLOBAL_EVENT_MODULE_READY,       /**< Sub-module enters READY state */
    GLOBAL_EVENT_MODULE_RUNNING,     /**< Sub-module enters RUNNING state */
    GLOBAL_EVENT_MODULE_ERROR,       /**< Sub-module enters ERROR state */
    GLOBAL_EVENT_MODULE_STOPPED,     /**< Sub-module enters IDLE/STOPPED state */
    GLOBAL_EVENT_SYSTEM_START,        /**< Trigger system-wide startup */
    GLOBAL_EVENT_SYSTEM_STOP,         /**< Trigger system-wide shutdown */
    GLOBAL_EVENT_SYSTEM_SHUTDOWN,     /**< Trigger emergency system shutdown */
    GLOBAL_EVENT_MAX                  /**< Boundary for event validation */
} global_event_t;

// ==========================================================================
// Callback Definitions (Reserved for Event Bus Integration)
// ==========================================================================
/**
 * @brief   Prototype for global state change callback
 * @details Notifies upper layer when global system state changes
 *
 * @param   old_state   Previous global state
 * @param   new_state   New global state
 * @param   user_data   Custom user data passed during initialization
 *
 * @note    Callback executes lock-free, keep logic non-blocking
 */
typedef void (*global_state_change_cb_t)(global_state_t old_state,
                                          global_state_t new_state,
                                          void *user_data);

/**
 * @brief   Prototype for global event notification callback
 * @details Publishes global events to Event Bus or upper layer
 *
 * @param   event       Triggered global event
 * @param   module_name Name of related sub-module (NULL for system events)
 * @param   user_data   Custom user data passed during initialization
 */
typedef void (*global_event_notify_cb_t)(global_event_t event,
                                          const char *module_name,
                                          void *user_data);

// ==========================================================================
// Global FSM Configuration
// ==========================================================================
/**
 * @brief   Global FSM initialization configuration
 * @details User-configurable parameters for global state machine
 */
typedef struct {
    uint32_t max_modules;               /**< Maximum supported sub-modules */
    global_state_change_cb_t state_cb;  /**< Global state change notification callback */
    global_event_notify_cb_t event_cb;  /**< Global event publish callback */
    void *user_data;                     /**< Private user context data */
} global_fsm_config_t;

// ==========================================================================
// Public Core APIs (Sorted by lifecycle: Init → Register → Operate → Deinit)
// ==========================================================================

/**
 * @brief   Initialize and create Global FSM instance
 * @param   config      Pointer to global FSM configuration structure
 * @param   out_handle  Pointer to store created Global FSM handle
 * @return  0 = success, negative value = error code
 *
 * @pre     config and out_handle must not be NULL
 * @post    Global FSM initialized with INIT state, module array allocated
 *
 * @note    Uses default max module count (16) if config value is 0
 * @warning Single instance only, do not initialize multiple times
 * @thread_safety Yes
 *
 * @example Usage demo:
 * @code
 * void state_callback(global_state_t old_s, global_state_t new_s, void *data) {
 *     // Handle global state change
 * }
 *
 * global_fsm_config_t config = {
 *     .max_modules = 8,
 *     .state_cb = state_callback,
 *     .event_cb = NULL,
 *     .user_data = NULL
 * };
 * global_fsm_handle_t g_handle;
 * int ret = global_fsm_init(&config, &g_handle);
 * @endcode
 */
int global_fsm_init(const global_fsm_config_t *config,
                    global_fsm_handle_t *out_handle);

/**
 * @brief   Register a sub-module FSM to Global FSM management
 * @param   handle        Global FSM instance handle
 * @param   module_name   Unique name of the sub-module
 * @param   module_fsm    Sub-module FSM handle
 * @param   is_critical   Mark as critical module (true = error triggers global ERROR)
 * @return  0 = success, negative value = error code
 *
 * @pre     All input parameters must be valid and non-NULL
 * @pre     Module name must be unique (no duplicate registration)
 * @post    Sub-module added to management list, global state recalculated
 *
 * @note    Bind global_fsm_on_module_state_change() to sub-module FSM first
 * @thread_safety Yes
 */
int global_fsm_register_module(global_fsm_handle_t handle,
                               const char *module_name,
                               module_fsm_handle_t module_fsm,
                               bool is_critical);

/**
 * @brief   Post global system event to drive state transitions
 * @param   handle  Global FSM instance handle
 * @param   event   Valid global system event
 * @return  0 = success, negative value = error code
 *
 * @pre     handle must be valid, event in valid range
 * @post    Global state updated and sub-modules notified if needed
 *
 * @note    Lock-free sub-module event posting to prevent deadlocks
 * @thread_safety Yes
 */
int global_fsm_post_event(global_fsm_handle_t handle, global_event_t event);

/**
 * @brief   Get current global system state
 * @param   handle  Global FSM instance handle
 * @return  Current global state (GLOBAL_STATE_INVALID if handle is NULL)
 *
 * @pre     handle must be a valid initialized instance
 * @post    No modification to FSM state or context
 *
 * @thread_safety Yes
 */
global_state_t global_fsm_get_state(global_fsm_handle_t handle);

/**
 * @brief   Deinitialize Global FSM and release all resources
 * @param   handle  Global FSM instance handle
 * @return  0 = success, negative value = error code
 *
 * @pre     handle must be a valid initialized instance
 * @post    All memory freed, mutex destroyed, context invalidated
 *
 * @note    Must be called to prevent memory leaks
 * @thread_safety Yes
 */
int global_fsm_deinit(global_fsm_handle_t handle);

/**
 * @brief   Convert global state enum to human-readable string (for logging)
 * @param   state   Global state enumeration value
 * @return  State name string (UNKNOWN for invalid values)
 *
 * @thread_safety Yes
 */
const char* global_state_to_str(global_state_t state);

/**
 * @brief   Convert global event enum to human-readable string (for logging)
 * @param   event   Global event enumeration value
 * @return  Event name string (UNKNOWN for invalid values)
 *
 * @thread_safety Yes
 */
const char* global_event_to_str(global_event_t event);

/**
 * @brief   Standard sub-module state change bridge callback
 * @details Core interface between sub-module FSM and Global FSM
 *
 * @param   module_name Name of the sub-module with state change
 * @param   old_state   Previous sub-module state
 * @param   new_state   New sub-module state
 * @param   user_data   Global FSM handle (passed during callback registration)
 *
 * @pre     Must be bound to all sub-module FSM state callbacks
 * @pre     user_data must be the valid Global FSM handle
 * @post    Sub-module state cached, global state recalculated
 *
 * @note    This is the mandatory bridge for sub-module state synchronization
 * @warning Do not modify this callback implementation
 * @thread_safety Yes
 */
void global_fsm_on_module_state_change(const char *module_name,
                                        module_state_t old_state,
                                        module_state_t new_state,
                                        void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* GLOBAL_FSM_H */