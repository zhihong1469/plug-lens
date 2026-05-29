/* SPDX-License-Identifier: MIT */
/**
 * @file    initcall.c
 * @brief   Implementation of automatic initialization framework
 * @details Low-level framework implementation:
 *          1. Traverse GCC custom sections by priority levels
 *          2. Execute all registered initialization functions
 *          3. No business logic coupling, pure underlying framework
 *          4. Integrated log system for initialization status monitoring
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "initcall.h"
#include "log.h"

/**
 * @brief   Iterate and execute initialization functions for single priority level
 * @param   level   Priority level number
 * @details Helper macro to eliminate duplicate code
 *          Automatically counts successfully initialized modules
 */
#define DO_INIT_LEVEL(level) do { \
    initcall_t *fn; \
    for (fn = __start_my_initcall_##level; fn < __stop_my_initcall_##level; fn++) { \
        (*fn)(); \
        init_count++; \
        LOG_I("[InitCall] Priority %d module initialized successfully", level); \
    } \
} while(0)

/**
 * @brief   Public API: Execute all module initializations in fixed priority order
 *
 * Core execution sequence (strict order, no modification allowed):
 * 1. Priority 0: System initialization
 * 2. Priority 1: Bus initialization
 * 3. Priority 2: Device initialization (capture modules)
 * 4. Priority 3: Service initialization (vision modules)
 * 5. Priority 4: Application initialization
 */
void do_initcalls(void)
{
    /* Counter for total initialized modules */
    int32_t init_count = 0;

    LOG_I("[InitCall] Starting priority-based automatic initialization");

    /* Strict priority execution order */
    DO_INIT_LEVEL(0);
    DO_INIT_LEVEL(1);
    DO_INIT_LEVEL(2);
    DO_INIT_LEVEL(3);
    DO_INIT_LEVEL(4);

    LOG_I("[InitCall] Automatic initialization completed, total modules: %d", init_count);
}

/* Undefine internal helper macro */
#undef DO_INIT_LEVEL