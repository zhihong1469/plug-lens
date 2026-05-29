/**
 * @file    module_fsm.h
 * @brief   Universal Base Finite State Machine for All Sub-Modules
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All functions are thread-safe with internal mutex protection.
 *          2. Call sequence: create → post events → get state → destroy.
 *          3. Single FSM instance per module is recommended.
 *          4. State change callbacks execute outside lock context.
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
 *  module_fsm_destroy(capture_fsm);*          3. User-configurable state transition table
 *          4. Lock-free state change callback to prevent deadlocks
 *          5. Opaque handle for complete implementation encapsulation
 *          6. Seamless integration with global system FSM
 *
 */
#ifndef MODULE_FSM_H
#define MODULE_FSM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// Opaque Handle (Fully encapsulated internal implementation)
// ==========================================================================
/**
 * @brief   Opaque handle for FSM module instance
 * @details External modules only use this pointer, internal structure is hidden
 */
typedef void* module_fsm_handle_t;

// ==========================================================================
// Universal Module States (Shared by ALL sub-modules)
// ==========================================================================
/**
 * @brief   Enumeration of universal module lifecycle states
 * @details Standardized states for all plug-lens sub-modules
 */
typedef enum {
    MODULE_STATE_INVALID = 0,        /**< Invalid/undefined state */
    MODULE_STATE_IDLE,               /**< Idle state (created, uninitialized) */
    MODULE_STATE_INITIALIZING,       /**< Initialization in progress */
    MODULE_STATE_READY,              /**< Initialized and ready to start */
    MODULE_STATE_STARTING,           /**< Startup in progress */
    MODULE_STATE_RUNNING,            /**< Normal operating state */
    MODULE_STATE_PAUSING,            /**< Pause in progress */
    MODULE_STATE_PAUSED,             /**< Paused state */
    MODULE_STATE_STOPPING,           /**< Shutdown in progress */
    MODULE_STATE_ERROR,              /**< Fault/error state */
    MODULE_STATE_DEINITIALIZING,     /**< De-initialization in progress */
    MODULE_STATE_DEINIT,             /**< De-initialized state */
    MODULE_STATE_MAX                 /**< Boundary for state validation */
} module_state_t;

// ==========================================================================
// Universal Module Events (Drive state transition)
// ==========================================================================
/**
 * @brief   Enumeration of universal FSM trigger events
 * @details Standard events to control module state transitions
 */
typedef enum {
    MODULE_EVENT_INVALID = 0,        /**< Invalid/undefined event */
    MODULE_EVENT_INIT,               /**< Trigger module initialization */
    MODULE_EVENT_INIT_OK,            /**< Initialization successful */
    MODULE_EVENT_INIT_FAIL,          /**< Initialization failed */
    MODULE_EVENT_START,              /**< Trigger module start */
    MODULE_EVENT_START_OK,           /**< Startup successful */
    MODULE_EVENT_START_FAIL,         /**< Startup failed */
    MODULE_EVENT_PAUSE,              /**< Trigger module pause */
    MODULE_EVENT_PAUSE_OK,           /**< Pause successful */
    MODULE_EVENT_RESUME,             /**< Trigger module resume */
    MODULE_EVENT_RESUME_OK,          /**< Resume successful */
    MODULE_EVENT_STOP,               /**< Trigger module stop */
    MODULE_EVENT_STOP_OK,            /**< Stop successful */
    MODULE_EVENT_ERROR,              /**< Report module error */
    MODULE_EVENT_ERROR_CLEAR,        /**< Clear module error state */
    MODULE_EVENT_DEINIT,             /**< Trigger module de-initialization */
    MODULE_EVENT_DEINIT_OK,          /**< De-initialization successful */
    MODULE_EVENT_MAX                 /**< Boundary for event validation */
} module_event_t;

// ==========================================================================
// State Transition Rule (Defined by business layer)
// ==========================================================================
/**
 * @brief   FSM state transition rule structure
 * @details Defines valid state changes triggered by specific events
 */
typedef struct {
    module_state_t current_state;    /**< Current state of the module */
    module_event_t  event;           /**< Event triggering the transition */
    module_state_t next_state;       /**< Target state after transition */
} module_state_trans_t;

// ==========================================================================
// Business Event Handler (Implemented by business layer)
// ==========================================================================
/**
 * @brief   Prototype for business event handler
 * @details Executes user logic before state transition
 *
 * @param   event       Triggered FSM event
 * @param   user_data   Custom user data passed during FSM creation
 * @return  0 = allow state transition, non-0 = reject transition
 *
 * @note    Handler executes within mutex lock context; keep logic lightweight.
 */
typedef int (*module_event_handler_t)(module_event_t event, void *user_data);

// ==========================================================================
// State Change Callback (Notify Global FSM/Event Bus)
// ==========================================================================
/**
 * @brief   Prototype for state change notification callback
 * @details Executes after atomic state transition (outside lock)
 *
 * @param   module_name Name of the module with state change
 * @param   old_state   Previous module state
 * @param   new_state   New module state
 * @param   user_data   Custom user data passed during FSM creation
 *
 * @note    Callback runs lock-free to avoid deadlocks; non-blocking logic only.
 * @warning Do not call FSM destroy/lock functions inside this callback.
 */
typedef void (*module_state_change_cb_t)(const char *module_name,
                                          module_state_t old_state,
                                          module_state_t new_state,
                                          void *user_data);

// ==========================================================================
// Module FSM Configuration
// ==========================================================================
/**
 * @brief   FSM module initialization configuration
 * @details All user-configurable parameters for FSM instance creation
 */
typedef struct {
    const char               *module_name;    /**< Unique name for the module */
    const module_state_trans_t *trans_table;  /**< User-defined state transition table */
    uint32_t                  trans_table_len; /**< Number of entries in transition table */
    module_event_handler_t    event_handler;   /**< Business logic event handler */
    module_state_change_cb_t  state_cb;        /**< State change notification callback */
    void                     *user_data;       /**< Private user context data */
} module_fsm_config_t;

// ==========================================================================
// Public Core APIs (Sorted by lifecycle: Create → Operate → Destroy)
// ==========================================================================

/**
 * @brief   Create and initialize a new FSM module instance
 * @param   config      Pointer to FSM configuration structure
 * @param   out_handle  Pointer to store the created FSM handle
 * @return  0 = success, negative value = error code
 *
 * @pre     config must not be NULL and contain valid transition table
 * @pre     out_handle must not be NULL
 * @post    FSM instance created with IDLE state and initialized mutex
 *
 * @note    Allocates dynamic memory for FSM context
 * @warning Do not create multiple instances for the same module
 * @thread_safety Yes
 *
 * @example Usage demo:
 * @code
 * static const module_state_trans_t trans_table[] = {
 *     {MODULE_STATE_IDLE, MODULE_EVENT_INIT, MODULE_STATE_INITIALIZING}
 * };
 *
 * module_fsm_handle_t fsm_handle;
 * module_fsm_config_t config = {
 *     .module_name = "DEMO_MODULE",
 *     .trans_table = trans_table,
 *     .trans_table_len = sizeof(trans_table)/sizeof(trans_table[0]),
 *     .event_handler = demo_event_handler,
 *     .state_cb = demo_state_callback,
 *     .user_data = NULL
 * };
 *
 * int ret = module_fsm_create(&config, &fsm_handle);
 * @endcode
 */
int module_fsm_create(const module_fsm_config_t *config,
                      module_fsm_handle_t *out_handle);

/**
 * @brief   Post an event to trigger FSM state transition
 * @param   handle  FSM module instance handle
 * @param   event   Valid FSM event to trigger transition
 * @return  0 = success (transition allowed), non-0 = failure (rejected/error)
 *
 * @pre     handle must be a valid created FSM instance
 * @pre     event must be in valid range (MODULE_EVENT_INVALID < event < MODULE_EVENT_MAX)
 * @post    State updated atomically if transition is valid
 *
 * @note    Only official entry for state machine control
 * @note    Business handler runs before state transition
 * @thread_safety Yes
 */
int module_fsm_post_event(module_fsm_handle_t handle, module_event_t event);

/**
 * @brief   Get current state of the FSM module
 * @param   handle  FSM module instance handle
 * @return  Current module state (MODULE_STATE_INVALID if handle is NULL)
 *
 * @pre     handle must be a valid created FSM instance
 * @post    No change to FSM state or context
 *
 * @thread_safety Yes
 */
module_state_t module_fsm_get_state(module_fsm_handle_t handle);

/**
 * @brief   Get the unique name of the FSM module
 * @param   handle  FSM module instance handle
 * @return  Module name string (UNKNOWN if handle is NULL)
 *
 * @pre     handle must be a valid created FSM instance
 * @post    No change to FSM state or context
 *
 * @thread_safety Yes
 */
const char* module_fsm_get_name(module_fsm_handle_t handle);

/**
 * @brief   Destroy FSM instance and release all resources
 * @param   handle  FSM module instance handle
 * @return  0 = success, negative value = error code
 *
 * @pre     handle must be a valid created FSM instance
 * @post    Context memory freed, mutex destroyed, state set to DEINIT
 *
 * @note    Must be called to avoid memory leaks
 * @thread_safety Yes
 */
int module_fsm_destroy(module_fsm_handle_t handle);

/**
 * @brief   Convert module state enum to human-readable string
 * @param   state   Module state enumeration value
 * @return  State name string (UNKNOWN for invalid values)
 *
 * @thread_safety Yes
 */
const char* module_state_to_str(module_state_t state);

/**
 * @brief   Convert module event enum to human-readable string
 * @param   event   Module event enumeration value
 * @return  Event name string (UNKNOWN for invalid values)
 *
 * @thread_safety Yes
 */
const char* module_event_to_str(module_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* MODULE_FSM_H */