/* SPDX-License-Identifier: MIT */
/**
 * @file    initcall.h
 * @brief   Automatic initialization framework for Linux plug-in layer
 * @details Core features:
 *          1. Based on GCC custom section, implements automatic plug-in loading
 *          2. Supports priority-based initialization sequence control
 *          3. Fixed nested macro expansion failure with dual-stringify mechanism
 *          4. Decoupled architecture, suitable for embedded Linux plug-lens system
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All auto-initialization modules come from libplug.a plug-in layer
 *          2. Lower priority value means earlier execution order
 *          3. Call do_initcalls() once in main() for all initialization
 */
#ifndef __INITCALL_H__
#define __INITCALL_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Initialization function pointer type
 * @return  0 on success, negative value on failure
 * @note    All plug-in initialization functions must comply with this signature
 */
typedef int (*initcall_t)(void);

// ====================== Initialization Priority Definitions ======================
// Lower value = Higher priority = Earlier execution
// No characters allowed after the priority number

/** System level initialization (highest priority) */
#define INIT_SYS        0
/** Bus level initialization */
#define INIT_BUS        1
/** Device level initialization */
#define INIT_DEVICE     2
/** Service level initialization */
#define INIT_SERVICE    3
/** Application level initialization (lowest priority) */
#define INIT_APP        4

/************************** Core Macro Expansion Fix **************************/
/**
 * @brief   First-level stringify macro (internal use only)
 * @param   x   Macro to be stringified
 * @note    Solves GCC preprocessor nested macro expansion failure
 */
#define _STRINGIFY(x) #x

/**
 * @brief   Second-level stringify macro (public use)
 * @param   x   Macro to be expanded and stringified
 * @details Dual expansion mechanism: expand level first, then stringify
 *          Fix: Direct #level causes macro expansion failure
 */
#define STRINGIFY(x) _STRINGIFY(x)

/************************** Core Initialization Registration Macro **************************/
/**
 * @brief   Universal plug-in initialization registration macro
 * @param   level   Initialization priority (INIT_SYS/INIT_BUS/INIT_DEVICE/INIT_SERVICE/INIT_APP)
 * @param   fn      Plug-in initialization function name
 * @details Places function pointer into specified priority custom section
 *          used: Prevent compiler optimization removal of unused functions
 *          section: Assign GCC custom section for priority classification
 * @note    Call once per plug-in for automatic registration
 */
#define MODULE_INIT_LEVEL(level, fn) \
    static initcall_t __initcall_##fn \
    __attribute__((used, section(".my_initcall_" STRINGIFY(level)))) = fn

/************************** Convenience Registration Macros **************************/
#define MODULE_INIT_SYS(fn)      MODULE_INIT_LEVEL(INIT_SYS, fn)      /**< Convenience: System level init */
#define MODULE_INIT_BUS(fn)      MODULE_INIT_LEVEL(INIT_BUS, fn)      /**< Convenience: Bus level init */
#define MODULE_INIT_DEVICE(fn)   MODULE_INIT_LEVEL(INIT_DEVICE, fn)   /**< Convenience: Device level init */
#define MODULE_INIT_SERVICE(fn)  MODULE_INIT_LEVEL(INIT_SERVICE, fn)  /**< Convenience: Service level init */
#define MODULE_INIT_APP(fn)      MODULE_INIT_LEVEL(INIT_APP, fn)      /**< Convenience: Application level init */

/************************** Custom Section Boundary Declaration **************************/
/**
 * @brief   Declare start/stop address of custom section
 * @param   level   Priority level number
 * @details Boundary symbols defined in linker script, used for iteration
 */
#define DECLARE_INITCALL_SEG(level) \
    extern initcall_t __start_my_initcall_##level[]; \
    extern initcall_t __stop_my_initcall_##level[];

// Declare section boundaries for all priority levels
DECLARE_INITCALL_SEG(0);
DECLARE_INITCALL_SEG(1);
DECLARE_INITCALL_SEG(2);
DECLARE_INITCALL_SEG(3);
DECLARE_INITCALL_SEG(4);

// Undefine internal helper macro to prevent namespace pollution
#undef DECLARE_INITCALL_SEG

/************************** Public API **************************/
/**
 * @brief   Execute all registered plug-in initializations in priority order
 *
 * @pre     All plug-ins have been registered with MODULE_INIT_* macros
 * @post    All modules initialized in fixed priority sequence
 * @note    Call ONLY ONCE in the main function during system startup
 * @thread_safety No
 *
 * @details Iterates all custom sections from priority 0 to 4
 *          Execution sequence: System → Bus → Device → Service → Application
 */
void do_initcalls(void);

#ifdef __cplusplus
}
#endif

#endif /* __INITCALL_H__ */