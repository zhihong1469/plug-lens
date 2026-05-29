/* SPDX-License-Identifier: MIT */
/**
 * @file    plugin_loader.c
 * @brief   Static plugin loader implementation for embedded plug-lens system
 * @details Core implementation features:
 *          1. Thread-safe plugin registration & lifecycle management
 *          2. Maximum 32 static plugins supported (configurable)
 *          3. Automatic rollback on initialization/start failure
 *          4. Reverse-order stop/deinit for resource safety
 *          5. Singleton context with mutex protection
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
// common/plugin/plugin_loader.c
#include "plugin_loader.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ==========================================================================
// Internal Macro Definitions
// ==========================================================================
/** Maximum number of supported static plugins */
#define MAX_PLUGINS 32

// ==========================================================================
// Internal Context Structure
// ==========================================================================
/**
 * @brief   Plugin loader global context (singleton)
 * @details Manages plugin list, lifecycle state, and thread safety
 */
typedef struct {
    const plugin_desc_t *plugins[MAX_PLUGINS];  /**< Registered plugin descriptor array */
    uint32_t plugin_count;                      /**< Current number of registered plugins */
    bool initialized;                           /**< Global initialization state flag */
    bool started;                               /**< Global running state flag */
    pthread_mutex_t lock;                       /**< Thread safety mutex */
} plugin_loader_context_t;

// ==========================================================================
// Internal Global Singleton Instance
// ==========================================================================
static plugin_loader_context_t g_loader_ctx = {
    .plugin_count = 0,
    .initialized = false,
    .started = false,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

// ==========================================================================
// Public API Implementation
// ==========================================================================

/**
 * @brief   Register a static plugin to the framework
 * @param   desc    Plugin descriptor pointer
 * @return  0 on success, -1 on invalid param/duplicate/full
 * @note    Thread-safe, duplicate plugin names are rejected
 */
int plugin_register(const plugin_desc_t *desc)
{
    if (desc == NULL || desc->name == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_loader_ctx.lock);

    // Check for duplicate plugin name
    for (uint32_t i = 0; i < g_loader_ctx.plugin_count; i++) {
        if (strcmp(g_loader_ctx.plugins[i]->name, desc->name) == 0) {
            pthread_mutex_unlock(&g_loader_ctx.lock);
            return -1;
        }
    }

    // Check plugin capacity limit
    if (g_loader_ctx.plugin_count >= MAX_PLUGINS) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return -1;
    }

    // Add new plugin to registry
    g_loader_ctx.plugins[g_loader_ctx.plugin_count++] = desc;

    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}

/**
 * @brief   Initialize all registered plugins
 * @return  0 on success, error code on failure
 * @note    Automatic rollback: deinit successful plugins if any init fails
 * @note    Thread-safe, idempotent operation
 */
int plugin_init_all(void)
{
    pthread_mutex_lock(&g_loader_ctx.lock);

    if (g_loader_ctx.initialized) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return 0;
    }

    // Initialize all plugins sequentially
    for (uint32_t i = 0; i < g_loader_ctx.plugin_count; i++) {
        const plugin_desc_t *desc = g_loader_ctx.plugins[i];
        if (desc->init != NULL) {
            int ret = desc->init();
            if (ret != 0) {
                // Rollback: deinit all previously initialized plugins
                for (uint32_t j = 0; j < i; j++) {
                    const plugin_desc_t *rollback_desc = g_loader_ctx.plugins[j];
                    if (rollback_desc->deinit != NULL) {
                        rollback_desc->deinit();
                    }
                }
                pthread_mutex_unlock(&g_loader_ctx.lock);
                return ret;
            }
        }
    }

    g_loader_ctx.initialized = true;
    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}

/**
 * @brief   Start all initialized plugins
 * @return  0 on success, -1 if not initialized, error code on failure
 * @note    Automatic rollback: stop successful plugins if any start fails
 * @note    Thread-safe, idempotent operation
 */
int plugin_start_all(void)
{
    pthread_mutex_lock(&g_loader_ctx.lock);

    if (!g_loader_ctx.initialized) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return -1;
    }

    if (g_loader_ctx.started) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return 0;
    }

    // Start all plugins sequentially
    for (uint32_t i = 0; i < g_loader_ctx.plugin_count; i++) {
        const plugin_desc_t *desc = g_loader_ctx.plugins[i];
        if (desc->start != NULL) {
            int ret = desc->start();
            if (ret != 0) {
                // Rollback: stop all previously started plugins
                for (uint32_t j = 0; j < i; j++) {
                    const plugin_desc_t *rollback_desc = g_loader_ctx.plugins[j];
                    if (rollback_desc->stop != NULL) {
                        rollback_desc->stop();
                    }
                }
                pthread_mutex_unlock(&g_loader_ctx.lock);
                return ret;
            }
        }
    }

    g_loader_ctx.started = true;
    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}

/**
 * @brief   Stop all running plugins (reverse order)
 * @return  0 on success
 * @note    Reverse order to maintain dependency safety
 * @note    Thread-safe, idempotent operation
 */
int plugin_stop_all(void)
{
    pthread_mutex_lock(&g_loader_ctx.lock);

    if (!g_loader_ctx.started) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return 0;
    }

    // Stop plugins in REVERSE order of initialization
    for (int32_t i = g_loader_ctx.plugin_count - 1; i >= 0; i--) {
        const plugin_desc_t *desc = g_loader_ctx.plugins[i];
        if (desc->stop != NULL) {
            desc->stop();
        }
    }

    g_loader_ctx.started = false;
    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}

/**
 * @brief   Deinitialize all plugins and clear registry
 * @return  0 on success
 * @note    Auto-stops plugins before deinitialization
 * @note    Reverse order deinit, clears plugin registry
 * @note    Thread-safe, idempotent operation
 */
int plugin_deinit_all(void)
{
    pthread_mutex_lock(&g_loader_ctx.lock);

    if (!g_loader_ctx.initialized) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return 0;
    }

    // Auto-stop running plugins first
    if (g_loader_ctx.started) {
        plugin_stop_all();
    }

    // Deinitialize plugins in REVERSE order
    for (int32_t i = g_loader_ctx.plugin_count - 1; i >= 0; i--) {
        const plugin_desc_t *desc = g_loader_ctx.plugins[i];
        if (desc->deinit != NULL) {
            desc->deinit();
        }
    }

    // Reset framework state
    g_loader_ctx.initialized = false;
    g_loader_ctx.plugin_count = 0;
    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}