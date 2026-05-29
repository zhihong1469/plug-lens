/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           module_fsm.c
 * @brief          Universal Base Module FSM Implementation
 * @details
 *  1. Thread-safe mutex protection for state transition
 *  2. Configurable state transition table
 *  3. Lock-free callback execution (avoid deadlock)
 *  4. Full null pointer & parameter validation
 *  5. Seamless connection with Global FSM
 * @author luo
 * @date 2026
 ******************************************************************************
 */
#include "module_fsm.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ==========================================================================
// Internal Configuration Macros
// ==========================================================================
#define MODULE_NAME_MAX_LEN    32

// ==========================================================================
// Internal FSM Context (Encapsulated, opaque to upper layer)
// ==========================================================================
typedef struct {
    char                    module_name[MODULE_NAME_MAX_LEN];
    module_state_t          current_state;
    const module_state_trans_t *trans_table;
    uint32_t                trans_table_len;
    module_event_handler_t  event_handler;
    module_state_change_cb_t state_cb;
    void                    *user_data;
    pthread_mutex_t         lock;       /**< Thread-safe mutex */
} module_fsm_context_t;

// ==========================================================================
// String Mapping Tables (For Log Printing)
// ==========================================================================
static const char* g_module_state_str[] = {
    [MODULE_STATE_INVALID]       = "INVALID",
    [MODULE_STATE_IDLE]          = "IDLE",
    [MODULE_STATE_INITIALIZING]  = "INITIALIZING",
    [MODULE_STATE_READY]         = "READY",
    [MODULE_STATE_STARTING]      = "STARTING",
    [MODULE_STATE_RUNNING]       = "RUNNING",
    [MODULE_STATE_PAUSING]       = "PAUSING",
    [MODULE_STATE_PAUSED]        = "PAUSED",
    [MODULE_STATE_STOPPING]      = "STOPPING",
    [MODULE_STATE_ERROR]         = "ERROR",
    [MODULE_STATE_DEINITIALIZING]= "DEINITIALIZING",
    [MODULE_STATE_DEINIT]        = "DEINIT",
};

static const char* g_module_event_str[] = {
    [MODULE_EVENT_INVALID]       = "INVALID",
    [MODULE_EVENT_INIT]          = "INIT",
    [MODULE_EVENT_INIT_OK]       = "INIT_OK",
    [MODULE_EVENT_INIT_FAIL]     = "INIT_FAIL",
    [MODULE_EVENT_START]         = "START",
    [MODULE_EVENT_START_OK]      = "START_OK",
    [MODULE_EVENT_START_FAIL]    = "START_FAIL",
    [MODULE_EVENT_PAUSE]         = "PAUSE",
    [MODULE_EVENT_PAUSE_OK]      = "PAUSE_OK",
    [MODULE_EVENT_RESUME]        = "RESUME",
    [MODULE_EVENT_RESUME_OK]     = "RESUME_OK",
    [MODULE_EVENT_STOP]          = "STOP",
    [MODULE_EVENT_STOP_OK]       = "STOP_OK",
    [MODULE_EVENT_ERROR]         = "ERROR",
    [MODULE_EVENT_ERROR_CLEAR]   = "ERROR_CLEAR",
    [MODULE_EVENT_DEINIT]        = "DEINIT",
    [MODULE_EVENT_DEINIT_OK]     = "DEINIT_OK",
};

// ==========================================================================
// Internal Helper Functions Declaration
// ==========================================================================
static bool _module_fsm_check_trans(module_fsm_context_t *ctx,
                                     module_event_t event,
                                     module_state_t *out_next_state);

// ==========================================================================
// Public API Implementation
// ==========================================================================

/**
 * @brief Create module FSM instance
 */
int module_fsm_create(const module_fsm_config_t *config,
                      module_fsm_handle_t *out_handle)
{
    // Parameter validation
    if (config == NULL || out_handle == NULL || config->module_name == NULL) {
        LOG_E("Module FSM: Create failed, null pointer input");
        return -1;
    }
    if (config->trans_table == NULL || config->trans_table_len == 0) {
        LOG_E("Module FSM: Transition table invalid");
        return -1;
    }

    // Allocate context memory
    module_fsm_context_t *ctx = (module_fsm_context_t*)malloc(sizeof(module_fsm_context_t));
    if (ctx == NULL) {
        LOG_E("Module FSM: Malloc context failed");
        return -1;
    }
    memset(ctx, 0, sizeof(module_fsm_context_t));

    // Copy configuration
    strncpy(ctx->module_name, config->module_name, MODULE_NAME_MAX_LEN - 1);
    ctx->trans_table       = config->trans_table;
    ctx->trans_table_len   = config->trans_table_len;
    ctx->event_handler     = config->event_handler;
    ctx->state_cb          = config->state_cb;
    ctx->user_data         = config->user_data;

    // Initial state: IDLE
    ctx->current_state     = MODULE_STATE_IDLE;

    // Initialize thread-safe mutex
    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        LOG_E("Module FSM: Mutex init failed");
        free(ctx);
        return -1;
    }

    *out_handle = (module_fsm_handle_t)ctx;
    LOG_I("Module FSM: [%s] created, initial state → %s", 
          ctx->module_name, module_state_to_str(ctx->current_state));
    return 0;
}

/**
 * @brief Post event to drive state transition
 */
int module_fsm_post_event(module_fsm_handle_t handle, module_event_t event)
{
    // Parameter validation
    if (handle == NULL || event <= MODULE_EVENT_INVALID || event >= MODULE_EVENT_MAX) {
        LOG_E("Module FSM: Invalid event or handle");
        return -1;
    }
    module_fsm_context_t *ctx = (module_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    LOG_D("Module FSM: [%s] recv event → %s | current state → %s", 
          ctx->module_name, 
          module_event_to_str(event), 
          module_state_to_str(ctx->current_state));

    // Step 1: Check if state transition is valid
    module_state_t next_state = MODULE_STATE_INVALID;
    if (!_module_fsm_check_trans(ctx, event, &next_state)) {
        LOG_W("Module FSM: [%s] invalid trans: %s + %s", 
              ctx->module_name, 
              module_state_to_str(ctx->current_state), 
              module_event_to_str(event));
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    // Step 2: Execute business event handler (if exists)
    int ret = 0;
    if (ctx->event_handler != NULL) {
        ret = ctx->event_handler(event, ctx->user_data);
        if (ret != 0) {
            LOG_E("Module FSM: [%s] business handler failed, ret=%d", 
                  ctx->module_name, ret);
            pthread_mutex_unlock(&ctx->lock);
            return ret;
        }
    }

    // Step 3: Atomic state transition
    module_state_t old_state = ctx->current_state;
    ctx->current_state = next_state;

    LOG_I("Module FSM: [%s] state change → %s → %s", 
          ctx->module_name, 
          module_state_to_str(old_state), 
          module_state_to_str(next_state));

    pthread_mutex_unlock(&ctx->lock);

    // Step 4: Notify upper layer (OUT OF LOCK, avoid deadlock)
    if (ctx->state_cb != NULL) {
        ctx->state_cb(ctx->module_name, old_state, next_state, ctx->user_data);
    }

    return 0;
}

/**
 * @brief Get current module state
 */
module_state_t module_fsm_get_state(module_fsm_handle_t handle)
{
    if (handle == NULL) {
        return MODULE_STATE_INVALID;
    }
    module_fsm_context_t *ctx = (module_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    module_state_t state = ctx->current_state;
    pthread_mutex_unlock(&ctx->lock);

    return state;
}

/**
 * @brief Get module name
 */
const char* module_fsm_get_name(module_fsm_handle_t handle)
{
    if (handle == NULL) {
        return "UNKNOWN";
    }
    module_fsm_context_t *ctx = (module_fsm_context_t*)handle;
    return ctx->module_name;
}

/**
 * @brief Destroy module FSM
 */
int module_fsm_destroy(module_fsm_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    module_fsm_context_t *ctx = (module_fsm_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    ctx->current_state = MODULE_STATE_DEINIT;
    pthread_mutex_unlock(&ctx->lock);

    // Release resources
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);

    LOG_I("Module FSM: Destroyed successfully");
    return 0;
}

/**
 * @brief State to string
 */
const char* module_state_to_str(module_state_t state)
{
    if (state < MODULE_STATE_INVALID || state >= MODULE_STATE_MAX) {
        return "UNKNOWN";
    }
    return g_module_state_str[state];
}

/**
 * @brief Event to string
 */
const char* module_event_to_str(module_event_t event)
{
    if (event < MODULE_EVENT_INVALID || event >= MODULE_EVENT_MAX) {
        return "UNKNOWN";
    }
    return g_module_event_str[event];
}

// ==========================================================================
// Internal Helper: Check State Transition Validity
// ==========================================================================
static bool _module_fsm_check_trans(module_fsm_context_t *ctx,
                                     module_event_t event,
                                     module_state_t *out_next_state)
{
    if (ctx == NULL || out_next_state == NULL) {
        return false;
    }

    // Traverse transition table to find matching rule
    for (uint32_t i = 0; i < ctx->trans_table_len; i++) {
        const module_state_trans_t *trans = &ctx->trans_table[i];
        if (trans->current_state == ctx->current_state &&
            trans->event == event) {
            *out_next_state = trans->next_state;
            return true;
        }
    }

    return false;
}