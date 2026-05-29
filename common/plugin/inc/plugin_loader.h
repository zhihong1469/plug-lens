/* SPDX-License-Identifier: MIT */
/**
 * @file    plugin_loader.h
 * @brief   Static plugin loader framework for embedded plug-lens Vision AI terminal
 * @details Core capabilities:
 *          1. Static plugin architecture (eliminates dynamic library overhead)
 *          2. Unified plugin lifecycle management interface
 *          3. Lightweight design optimized for resource-constrained embedded Linux
 *          4. Compile-time plugin registration, zero runtime loading cost
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global lifecycle rules:
 *          1. Register plugins first via plugin_register()
 *          2. Execution order: init_all → start_all → stop_all → deinit_all
 *          3. Static linking only, no dynamic library dependency
 *          4. All public APIs are thread-safe
 */
// common/plugin/plugin_loader.h
#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// Static Plugin Interface Definitions (Embedded-Optimized)
// ==========================================================================

/**
 * @brief   Plugin initialization function pointer type
 * @return  0 on success, negative value on failure
 */
typedef int (*plugin_init_func_t)(void);

/**
 * @brief   Plugin start function pointer type
 * @return  0 on success, negative value on failure
 */
typedef int (*plugin_start_func_t)(void);

/**
 * @brief   Plugin stop function pointer type
 * @return  0 on success, negative value on failure
 */
typedef int (*plugin_stop_func_t)(void);

/**
 * @brief   Plugin de-initialization (destroy) function pointer type
 * @return  0 on success, negative value on failure
 */
typedef int (*plugin_deinit_func_t)(void);

/**
 * @brief   Plugin descriptor structure
 * @details Defines complete lifecycle and identity of a static plugin
 * @note    All function pointers can be NULL if unused
 */
typedef struct {
    const char *name;           /**< Unique plugin name string */
    plugin_init_func_t init;    /**< Plugin initialization callback */
    plugin_start_func_t start;  /**< Plugin start/run callback */
    plugin_stop_func_t stop;    /**< Plugin stop/pause callback */
    plugin_deinit_func_t deinit;/**< Plugin resource release callback */
} plugin_desc_t;

// ==========================================================================
// Plugin Loader Public APIs
// ==========================================================================

/**
 * @brief   Register a static plugin to the framework
 * @param   desc    Pointer to plugin descriptor
 * @return  0 on success, -1 on invalid parameter/registration failure
 *
 * @pre     Called during system initialization (compile-time registration)
 * @post    Plugin added to management list
 * @thread_safety Yes
 */
int plugin_register(const plugin_desc_t *desc);

/**
 * @brief   Initialize all registered plugins
 * @return  0 on success, negative value on initialization failure
 *
 * @pre     All plugins registered via plugin_register()
 * @post    All plugins in initialized state
 * @thread_safety Yes
 */
int plugin_init_all(void);

/**
 * @brief   Start all initialized plugins
 * @return  0 on success, negative value on start failure
 *
 * @pre     plugin_init_all() executed successfully
 * @post    All plugins in running state
 * @thread_safety Yes
 */
int plugin_start_all(void);

/**
 * @brief   Stop all running plugins
 * @return  0 on success, negative value on stop failure
 *
 * @pre     Plugins started via plugin_start_all()
 * @post    All plugins in stopped state
 * @thread_safety Yes
 */
int plugin_stop_all(void);

/**
 * @brief   De-initialize and release resources for all plugins
 * @return  0 on success, negative value on deinit failure
 *
 * @pre     Plugins stopped via plugin_stop_all()
 * @post    All plugin resources released, framework reset
 * @thread_safety Yes
 */
int plugin_deinit_all(void);

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_LOADER_H */