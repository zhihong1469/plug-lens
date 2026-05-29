/* SPDX-License-Identifier: MIT */
#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file    main.h
 * @brief   Main Application System Public Interface
 * @details Exposes thread-safe system-level exit API for all business modules.
 *          Only contains core system trigger functions, no business logic definitions.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. This is the ONLY public interface from main module to business components.
 *          2. Function is thread-safe and signal-safe.
 */

/**
 * @brief   Trigger system graceful soft exit
 * @details Thread-safe and signal-safe exit trigger for all business modules.
 *          Publishes system shutdown event and activates exit pipeline.
 *
 * @return  None
 *
 * @pre     System core must be initialized successfully.
 * @post    System enters graceful shutdown process.
 *
 * @note    Called by business modules to request normal system exit.
 * @warning Do not call in fatal crash handlers (use internal crash logic instead).
 * @thread_safety Yes (Signal-safe & thread-safe)
 */
void app_trigger_soft_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */