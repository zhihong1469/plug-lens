/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           module_fsm.h
 * @brief          Universal Base Finite State Machine for All Sub-Modules
 * @defgroup       MODULE_FSM
 * @details
 * 【Module FSM Iron Rules】
 *  1. Pure base class, NO specific business logic
 *  2. State transition table & event handler provided by upper business layer
 *  3. Full atomic state transition, thread-safe design
 *  4. Opaque handle, fully encapsulated internal implementation
 *  5. Callback decoupling: State change notify for Global FSM/Event Bus
 *
 * 【Core Features】
 *  1. Universal state/event definition for all sub-modules
 *  2. Configurable state transition table (business-defined)
 *  3. Thread-safe event post & state query
 *  4. State change callback (seamless connect to Global FSM)
 *  5. Lock-free callback execution to avoid deadlock
 *
 * @example
 *  // ========== 1. Define Business State Transition Table ==========
 *  static const module_state_trans_t g_capture_trans[] = {
 *      {MODULE_STATE_IDLE,    MODULE_EVENT_INIT,    MODULE_STATE_INITIALIZING},
 *      {MODULE_STATE_INITIALIZING, MODULE_EVENT_INIT_OK, MODULE_STATE_READY},
 *      {MODULE_STATE_READY,   MODULE_EVENT_START,   MODULE_STATE_RUNNING},
 *      {MODULE_STATE_RUNNING, MODULE_EVENT_STOP,    MODULE_STATE_IDLE},
 *      {MODULE_STATE_RUNNING, MODULE_EVENT_ERROR,   MODULE_STATE_ERROR},
 *  };
 * 
 *  // ========== 2. Business Event Handler ==========
 *  int capture_event_handler(module_event_t event, void *user_data) {
 *      // Execute business logic here (non-blocking)
 *      return 0; // 0=allow transition, non-0=reject
 *  }
 * 
 *  // ========== 3. Create Module FSM ==========
 *  module_fsm_handle_t capture_fsm;
 *  module_fsm_config_t cfg = {
 *      .module_name    = "CAPTURE",
 *      .trans_table    = g_capture_trans,
 *      .trans_table_len = sizeof(g_capture_trans)/sizeof(module_state_trans_t),
 *      .event_handler  = capture_event_handler,
 *      .state_cb       = global_fsm_on_module_state_change, // Bind to Global FSM
 *      .user_data      = global_fsm_handle,
 *  };
 *  module_fsm_create(&cfg, &capture_fsm);
 * 
 *  // ========== 4. Post Event to Drive State ==========
 *  module_fsm_post_event(capture_fsm, MODULE_EVENT_INIT);
 * 
 *  // ========== 5. Get State/Name ==========
 *  module_state_t state = module_fsm_get_state(capture_fsm);
 *  const char *name = module_fsm_get_name(capture_fsm);
 * 
 *  // ========== 6. Destroy FSM ==========
 *  module_fsm_destroy(capture_fsm);
 * 
 * @author         luo
 * @date           2026
 ******************************************************************************
 */
#ifndef MODULE_FSM_H
#define MODULE_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// ==========================================================================
// Opaque Handle (Fully encapsulated internal implementation)
// ==========================================================================
typedef void* module_fsm_handle_t;

// ==========================================================================
// Universal Module States (Shared by ALL sub-modules)
// ==========================================================================
typedef enum {
    MODULE_STATE_INVALID = 0,
    MODULE_STATE_IDLE,           /**< Idle (created, not initialized) */
    MODULE_STATE_INITIALIZING,   /**< Initializing */
    MODULE_STATE_READY,          /**< Ready (wait for start command) */
    MODULE_STATE_STARTING,       /**< Starting */
    MODULE_STATE_RUNNING,        /**< Running */
    MODULE_STATE_PAUSING,        /**< Pausing */
    MODULE_STATE_PAUSED,         /**< Paused */
    MODULE_STATE_STOPPING,       /**< Stopping */
    MODULE_STATE_ERROR,          /**< Error state */
    MODULE_STATE_DEINITIALIZING, /**< De-initializing */
    MODULE_STATE_DEINIT,         /**< De-initialized */
    MODULE_STATE_MAX
} module_state_t;

// ==========================================================================
// Universal Module Events (Drive state transition)
// ==========================================================================
typedef enum {
    MODULE_EVENT_INVALID = 0,
    MODULE_EVENT_INIT,           /**< Init command */
    MODULE_EVENT_INIT_OK,        /**< Init success */
    MODULE_EVENT_INIT_FAIL,      /**< Init failed */
    MODULE_EVENT_START,          /**< Start command */
    MODULE_EVENT_START_OK,       /**< Start success */
    MODULE_EVENT_START_FAIL,     /**< Start failed */
    MODULE_EVENT_PAUSE,          /**< Pause command */
    MODULE_EVENT_PAUSE_OK,       /**< Pause success */
    MODULE_EVENT_RESUME,         /**< Resume command */
    MODULE_EVENT_RESUME_OK,      /**< Resume success */
    MODULE_EVENT_STOP,           /**< Stop command */
    MODULE_EVENT_STOP_OK,        /**< Stop success */
    MODULE_EVENT_ERROR,          /**< Error report */
    MODULE_EVENT_ERROR_CLEAR,    /**< Error clear */
    MODULE_EVENT_DEINIT,         /**< Deinit command */
    MODULE_EVENT_DEINIT_OK,      /**< Deinit success */
    MODULE_EVENT_MAX
} module_event_t;

// ==========================================================================
// State Transition Rule (Defined by business layer)
// ==========================================================================
typedef struct {
    module_state_t current_state; /**< Current state */
    module_event_t  event;         /**< Trigger event */
    module_state_t next_state;     /**< Target state */
} module_state_trans_t;

// ==========================================================================
// Business Event Handler (Implemented by business layer)
// Return: 0=allow transition, non-0=reject transition
// ==========================================================================
typedef int (*module_event_handler_t)(module_event_t event, void *user_data);

// ==========================================================================
// State Change Callback (Notify Global FSM/Event Bus)
// ==========================================================================
typedef void (*module_state_change_cb_t)(const char *module_name,
                                          module_state_t old_state,
                                          module_state_t new_state,
                                          void *user_data);

// ==========================================================================
// Module FSM Configuration
// ==========================================================================
typedef struct {
    const char               *module_name;        /**< Unique module name */
    const module_state_trans_t *trans_table;      /**< State transition table */
    uint32_t                  trans_table_len;     /**< Transition table length */
    module_event_handler_t    event_handler;       /**< Business event handler */
    module_state_change_cb_t  state_cb;            /**< State change notify cb */
    void                     *user_data;           /**< Private user data */
} module_fsm_config_t;

// ==========================================================================
// Public Core APIs
// ==========================================================================

/**
 * @brief  Create module FSM instance
 * @param  config     FSM configuration (business-defined)
 * @param  out_handle Output FSM handle
 * @return 0=success, negative=failure
 */
int module_fsm_create(const module_fsm_config_t *config,
                      module_fsm_handle_t *out_handle);

/**
 * @brief  Post event to drive state transition (ONLY entry)
 * @param  handle  Module FSM handle
 * @param  event   Trigger event
 * @return 0=success(allow trans), non-0=failure(reject trans)
 */
int module_fsm_post_event(module_fsm_handle_t handle, module_event_t event);

/**
 * @brief  Get current module state
 * @param  handle  Module FSM handle
 * @return Current state
 */
module_state_t module_fsm_get_state(module_fsm_handle_t handle);

/**
 * @brief  Get module unique name
 * @param  handle  Module FSM handle
 * @return Module name string
 */
const char* module_fsm_get_name(module_fsm_handle_t handle);

/**
 * @brief  Destroy module FSM, release resources
 * @param  handle  Module FSM handle
 * @return 0=success, negative=failure
 */
int module_fsm_destroy(module_fsm_handle_t handle);

/**
 * @brief  Convert module state to string (for log)
 * @param  state  Module state enum
 * @return State name string
 */
const char* module_state_to_str(module_state_t state);

/**
 * @brief  Convert module event to string (for log)
 * @param  event  Module event enum
 * @return Event name string
 */
const char* module_event_to_str(module_event_t event);

#endif /* MODULE_FSM_H */