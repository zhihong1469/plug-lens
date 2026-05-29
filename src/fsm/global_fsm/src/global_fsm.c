/**
 * @file    global_fsm.c
 * @brief   Global Master FSM Implementation
 * @details Internal implementation features:
 *          1. Thread-safe context protection with pthread mutex
 *          2. Automatic global state decision algorithm based on module status
 *          3. Lock-free event forwarding to sub-modules (deadlock prevention)
 *          4. Dynamic module management array with configurable capacity
 *          5. Full null pointer and parameter validation
 *          6. Callback-based decoupling for Event Bus integration
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "global_fsm.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ==========================================================================
// Internal Configuration Macros
// ==========================================================================
/** Default maximum number of managed sub-modules */
#define GLOBAL_FSM_MAX_MODULES_DEFAULT  16
/** Maximum length of sub-module name string */
#define MODULE_NAME_MAX_LEN             32

// ==========================================================================
// Internal Sub-module Information Structure
// ==========================================================================
/**
 * @brief   Internal sub-module management structure
 * @details Stores runtime status and attributes of registered sub-modules
 */
typedef struct {
    char name[MODULE_NAME_MAX_LEN];    /**< Unique sub-module name */
    module_fsm_handle_t fsm;           /**< Sub-module FSM handle */
    module_state_t current_state;       /**< Cached sub-module state */
    bool is_critical;                  /**< Critical module flag */
    bool registered;                    /**< Valid registration flag */
} module_info_t;

// ==========================================================================
// Global FSM Internal Context (Encapsulated)
// ==========================================================================
/**
 * @brief   Global FSM private context structure
 * @details Core runtime data for global state machine
 * @note    External code cannot access members directly
 */
typedef struct {
    global_fsm_config_t config;         /**< User configuration parameters */
    global_state_t current_state;       /**< Current global system state */
    module_info_t *modules;             /**< Managed sub-module array */
    uint32_t module_count;              /**< Number of registered modules */
    uint32_t max_modules;               /**< Maximum supported module count */
    pthread_mutex_t lock;               /**< Thread safety mutex */
} global_fsm_context_t;

// ==========================================================================
// String Mapping Tables (For Log Printing)
// ==========================================================================
/** String mapping table for global state logging */
static const char* g_global_state_str[] = {
    [GLOBAL_STATE_INVALID]       = "INVALID",
    [GLOBAL_STATE_INIT]          = "INIT",
    [GLOBAL_STATE_READY]         = "READY",
    [GLOBAL_STATE_RUNNING]       = "RUNNING",
    [GLOBAL_STATE_DEGRADED]      = "DEGRADED",
    [GLOBAL_STATE_ERROR]         = "ERROR",
    [GLOBAL_STATE_SHUTTING_DOWN] = "SHUTTING_DOWN",
    [GLOBAL_STATE_OFF]           = "OFF",
};

/** String mapping table for global event logging */
static const char* g_global_event_str[] = {
    [GLOBAL_EVENT_INVALID]        = "INVALID",
    [GLOBAL_EVENT_MODULE_READY]   = "MODULE_READY",
    [GLOBAL_EVENT_MODULE_RUNNING] = "MODULE_RUNNING",
    [GLOBAL_EVENT_MODULE_ERROR]   = "MODULE_ERROR",
    [GLOBAL_EVENT_MODULE_STOPPED] = "MODULE_STOPPED",
    [GLOBAL_EVENT_SYSTEM_START]   = "SYSTEM_START",
    [GLOBAL_EVENT_SYSTEM_STOP]    = "SYSTEM_STOP",
    [GLOBAL_EVENT_SYSTEM_SHUTDOWN]= "SYSTEM_SHUTDOWN",
};

// ==========================================================================
// Internal Helper Functions Declaration
// ==========================================================================
/**
 * @brief   Recalculate and update global system state
 *
 * @param   ctx  Pointer to Global FSM internal context
 *
 * Workflow:
 * 1. Traverse all registered modules to check status
 * 2. Evaluate critical/non-critical error conditions
 * 3. Determine new global state by priority rules
 * 4. Trigger state transition if changed
 *
 * @note    Core state decision algorithm of Global FSM
 */
static void _global_fsm_update_global_state(global_fsm_context_t *ctx);

/**
 * @brief   Notify global event to upper layer/Event Bus
 *
 * @param   ctx          Pointer to Global FSM context
 * @param   event        Global event to publish
 * @param   module_name  Related sub-module name (NULL for system events)
 */
static void _global_fsm_notify_event(global_fsm_context_t *ctx,
                                      global_event_t event,
                                      const char *module_name);

/**
 * @brief   Execute atomic global state transition
 *
 * @param   ctx         Pointer to Global FSM context
 * @param   new_state   Target global state
 *
 * Workflow:
 * 1. Lock context and validate state change
 * 2. Update global state atomically
 * 3. Unlock and notify upper layer via callback
 */
static void _global_fsm_change_state(global_fsm_context_t *ctx,
                                      global_state_t new_state);

// ==========================================================================
// Public API Implementation (Lifecycle Order: Init → Register → Operate → Deinit)
// ==========================================================================
int global_fsm_init(const global_fsm_config_t *config,
                    global_fsm_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        LOG_E("Global FSM: Null pointer input");
        return -1;
    }

    // Allocate main context memory
    global_fsm_context_t *ctx = (global_fsm_context_t*)malloc(sizeof(global_fsm_context_t));
    if (ctx == NULL) {
        LOG_E("Global FSM: Malloc context failed");
        return -1;
    }
    memset(ctx, 0, sizeof(global_fsm_context_t));

    // Copy user configuration and set module limit
    memcpy(&ctx->config, config, sizeof(global_fsm_config_t));
    ctx->max_modules = (config->max_modules > 0) ? 
                        config->max_modules : GLOBAL_FSM_MAX_MODULES_DEFAULT;

    // Allocate module management array
    ctx->modules = (module_info_t*)malloc(ctx->max_modules * sizeof(module_info_t));
    if (ctx->modules == NULL) {
        LOG_E("Global FSM: Malloc modules array failed");
        free(ctx);
        return -1;
    }
    memset(ctx->modules, 0, ctx->max_modules * sizeof(module_info_t));

    // Initialize thread safety mutex
    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        LOG_E("Global FSM: Mutex init failed");
        free(ctx->modules);
        free(ctx);
        return -1;
    }

    // Set initial global state
    ctx->current_state = GLOBAL_STATE_INIT;
    ctx->module_count = 0;

    *out_handle = (global_fsm_handle_t)ctx;
    LOG_I("Global FSM: Initialized successfully, max modules=%u", ctx->max_modules);
    return 0;
}

int global_fsm_register_module(global_fsm_handle_t handle,
                               const char *module_name,
                               module_fsm_handle_t module_fsm,
                               bool is_critical)
{
    if (handle == NULL || module_name == NULL || module_fsm == NULL) {
        LOG_E("Global FSM: Register module with null pointer");
        return -1;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    // Check maximum module capacity
    if (ctx->module_count >= ctx->max_modules) {
        LOG_E("Global FSM: Max modules limit reached");
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    // Check for duplicate module name
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (strcmp(ctx->modules[i].name, module_name) == 0) {
            LOG_E("Global FSM: Module %s already registered", module_name);
            pthread_mutex_unlock(&ctx->lock);
            return -1;
        }
    }

    // Register new sub-module
    module_info_t *info = &ctx->modules[ctx->module_count];
    strncpy(info->name, module_name, MODULE_NAME_MAX_LEN - 1);
    info->fsm = module_fsm;
    info->current_state = module_fsm_get_state(module_fsm);
    info->is_critical = is_critical;
    info->registered = true;
    ctx->module_count++;

    pthread_mutex_unlock(&ctx->lock);

    LOG_I("Global FSM: Module %s registered (critical=%d)", module_name, is_critical);
    // Recalculate global state after new module registration
    _global_fsm_update_global_state(ctx);
    
    return 0;
}

int global_fsm_post_event(global_fsm_handle_t handle, global_event_t event)
{
    if (handle == NULL || event <= GLOBAL_EVENT_INVALID || event >= GLOBAL_EVENT_MAX) {
        LOG_E("Global FSM: Invalid event or handle");
        return -1;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    LOG_I("Global FSM: Post event → %s", global_event_to_str(event));

    // Thread-safe read of current global state
    pthread_mutex_lock(&ctx->lock);
    global_state_t current = ctx->current_state;
    pthread_mutex_unlock(&ctx->lock);

    switch (event) {
        case GLOBAL_EVENT_SYSTEM_START: {
            if (current != GLOBAL_STATE_READY) {
                LOG_W("Global FSM: Start failed, current state=%s", global_state_to_str(current));
                return -1;
            }
            /* Lock-free design:
             * Copy module handles to temporary array to avoid long mutex hold
             * Prevent deadlock when posting events to sub-modules
             */
            module_fsm_handle_t *temp_modules = NULL;
            uint32_t temp_count = 0;
            
            pthread_mutex_lock(&ctx->lock);
            temp_count = ctx->module_count;
            temp_modules = (module_fsm_handle_t*)malloc(temp_count * sizeof(module_fsm_handle_t));
            if (temp_modules != NULL) {
                for (uint32_t i = 0; i < temp_count; i++) {
                    temp_modules[i] = ctx->modules[i].fsm;
                }
            }
            pthread_mutex_unlock(&ctx->lock);

            // Post start event to all sub-modules (no lock held)
            if (temp_modules != NULL) {
                for (uint32_t i = 0; i < temp_count; i++) {
                    module_fsm_post_event(temp_modules[i], MODULE_EVENT_START);
                }
                free(temp_modules);
            }
            break;
        }

        case GLOBAL_EVENT_SYSTEM_STOP: {
            if (current != GLOBAL_STATE_RUNNING && current != GLOBAL_STATE_DEGRADED) {
                LOG_W("Global FSM: Stop failed, current state=%s", global_state_to_str(current));
                return -1;
            }
            /* Lock-free design:
             * Same handle copy mechanism to prevent deadlock during stop
             */
            module_fsm_handle_t *temp_modules_stop = NULL;
            uint32_t temp_count_stop = 0;
            
            pthread_mutex_lock(&ctx->lock);
            temp_count_stop = ctx->module_count;
            temp_modules_stop = (module_fsm_handle_t*)malloc(temp_count_stop * sizeof(module_fsm_handle_t));
            if (temp_modules_stop != NULL) {
                for (uint32_t i = 0; i < temp_count_stop; i++) {
                    temp_modules_stop[i] = ctx->modules[i].fsm;
                }
            }
            pthread_mutex_unlock(&ctx->lock);

            // Post stop event to all sub-modules (no lock held)
            if (temp_modules_stop != NULL) {
                for (uint32_t i = 0; i < temp_count_stop; i++) {
                    module_fsm_post_event(temp_modules_stop[i], MODULE_EVENT_STOP);
                }
                free(temp_modules_stop);
            }
            break;
        }

        case GLOBAL_EVENT_SYSTEM_SHUTDOWN:
            // Direct emergency state transition
            _global_fsm_change_state(ctx, GLOBAL_STATE_SHUTTING_DOWN);
            break;

        default:
            break;
    }

    // Publish event to upper layer (Event Bus)
    _global_fsm_notify_event(ctx, event, NULL);
    return 0;
}

global_state_t global_fsm_get_state(global_fsm_handle_t handle)
{
    if (handle == NULL) {
        return GLOBAL_STATE_INVALID;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    // Thread-safe state read with minimal lock time
    pthread_mutex_lock(&ctx->lock);
    global_state_t state = ctx->current_state;
    pthread_mutex_unlock(&ctx->lock);

    return state;
}

int global_fsm_deinit(global_fsm_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    global_fsm_context_t *ctx = (global_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    // Release module array memory
    free(ctx->modules);
    ctx->modules = NULL;
    ctx->module_count = 0;
    pthread_mutex_unlock(&ctx->lock);

    // Release system resources
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);

    LOG_I("Global FSM: Deinitialized successfully");
    return 0;
}

const char* global_state_to_str(global_state_t state)
{
    if (state < GLOBAL_STATE_INVALID || state >= GLOBAL_STATE_MAX) {
        return "UNKNOWN";
    }
    return g_global_state_str[state];
}

const char* global_event_to_str(global_event_t event)
{
    if (event < GLOBAL_EVENT_INVALID || event >= GLOBAL_EVENT_MAX) {
        return "UNKNOWN";
    }
    return g_global_event_str[event];
}

// ==========================================================================
// Core Callback: Sub-module State Change Bridge
// ==========================================================================
void global_fsm_on_module_state_change(const char *module_name,
                                        module_state_t old_state,
                                        module_state_t new_state,
                                        void *user_data)
{
    global_fsm_context_t *ctx = (global_fsm_context_t*)user_data;
    if (ctx == NULL || module_name == NULL) {
        LOG_E("Global FSM: Module callback null pointer");
        return;
    }

    LOG_I("Global FSM: Module %s | %s → %s", 
          module_name, 
          module_state_to_str(old_state), 
          module_state_to_str(new_state));

    // Thread-safe update of cached module state
    pthread_mutex_lock(&ctx->lock);
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (strcmp(ctx->modules[i].name, module_name) == 0) {
            ctx->modules[i].current_state = new_state;
            break;
        }
    }
    pthread_mutex_unlock(&ctx->lock);

    // Recalculate global system state
    _global_fsm_update_global_state(ctx);

    // Map sub-module state to global event
    global_event_t evt = GLOBAL_EVENT_INVALID;
    switch (new_state) {
        case MODULE_STATE_READY:   evt = GLOBAL_EVENT_MODULE_READY;   break;
        case MODULE_STATE_RUNNING: evt = GLOBAL_EVENT_MODULE_RUNNING; break;
        case MODULE_STATE_ERROR:   evt = GLOBAL_EVENT_MODULE_ERROR;   break;
        case MODULE_STATE_IDLE:    evt = GLOBAL_EVENT_MODULE_STOPPED; break;
        default: break;
    }

    // Publish mapped global event
    if (evt != GLOBAL_EVENT_INVALID) {
        _global_fsm_notify_event(ctx, evt, module_name);
    }
}

// ==========================================================================
// Core: Global State Decision Algorithm
// ==========================================================================
static void _global_fsm_update_global_state(global_fsm_context_t *ctx)
{
    if (ctx == NULL) return;

    pthread_mutex_lock(&ctx->lock);

    bool all_ready = true;
    bool all_running = true;
    bool has_non_critical_err = false;
    bool has_critical_err = false;

    // Traverse all modules to evaluate system status
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (!ctx->modules[i].registered) continue;

        module_state_t s = ctx->modules[i].current_state;
        
        // Classify error types (critical vs non-critical)
        if (s == MODULE_STATE_ERROR) {
            if (ctx->modules[i].is_critical) {
                has_critical_err = true;
            } else {
                has_non_critical_err = true;
            }
        }

        if (s != MODULE_STATE_READY)   all_ready = false;
        if (s != MODULE_STATE_RUNNING) all_running = false;
    }

    /* State decision priority:
     * ERROR (critical fault) > DEGRADED (non-critical fault) > RUNNING > READY
     * Core rule for system stability and fault handling
     */
    global_state_t new_state = ctx->current_state;
    if (has_critical_err) {
        new_state = GLOBAL_STATE_ERROR;
    } else if (has_non_critical_err) {
        new_state = GLOBAL_STATE_DEGRADED;
    } else if (all_running && ctx->module_count > 0) {
        new_state = GLOBAL_STATE_RUNNING;
    } else if (all_ready && ctx->module_count > 0) {
        new_state = GLOBAL_STATE_READY;
    }

    pthread_mutex_unlock(&ctx->lock);

    // Execute state transition if changed
    if (new_state != ctx->current_state) {
        _global_fsm_change_state(ctx, new_state);
    }
}

// ==========================================================================
// Internal: Notify Global Event (Reserved for Event Bus)
// ==========================================================================
static void _global_fsm_notify_event(global_fsm_context_t *ctx,
                                      global_event_t event,
                                      const char *module_name)
{
    if (ctx == NULL) return;

    // Forward event to user-defined callback (Event Bus integration point)
    if (ctx->config.event_cb != NULL) {
        ctx->config.event_cb(event, module_name, ctx->config.user_data);
    }
}

// ==========================================================================
// Internal: Global State Transition
// ==========================================================================
static void _global_fsm_change_state(global_fsm_context_t *ctx,
                                      global_state_t new_state)
{
    if (ctx == NULL) return;

    pthread_mutex_lock(&ctx->lock);
    global_state_t old_state = ctx->current_state;
    
    // Skip duplicate state transition
    if (old_state == new_state) {
        pthread_mutex_unlock(&ctx->lock);
        return;
    }

    // Atomic global state update
    ctx->current_state = new_state;
    LOG_I("Global FSM: State Change | %s → %s", 
          global_state_to_str(old_state), 
          global_state_to_str(new_state));
    pthread_mutex_unlock(&ctx->lock);

    // Notify upper layer of state change
    if (ctx->config.state_cb != NULL) {
        ctx->config.state_cb(old_state, new_state, ctx->config.user_data);
    }
}