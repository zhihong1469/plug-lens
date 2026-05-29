/**
 * @file    led_indicator.h
 * @brief   LED Indicator Subclass Driver Header
 * @details Provides interface for hardware LED indicator control based on LED base class,
 *          supports standard ON/OFF control via Linux device node (/dev/ledx).
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All functions are not thread-safe unless marked specially.
 *          2. Call functions in order: create → init → deinit → destroy.
 *          3. Only one instance is allowed per LED device node.
 */
#ifndef __LED_INDICATOR_H__
#define __LED_INDICATOR_H__

#include "led_base.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Create an instance of LED indicator device
 * @details Allocate memory and initialize base class parameters for LED indicator.
 * @param   dev_path    Pointer to Linux LED device node path (e.g., /dev/100ask_led0).
 *                      Cannot be NULL, valid path must be provided.
 * @return  led_base_t* Pointer to LED base class handle on success;
 *                      NULL if memory allocation fails or invalid input.
 *
 * @pre     None (first interface to call).
 * @post    LED instance is allocated and in IDLE state, device file is not opened.
 * @note    This is the constructor for LED indicator subclass.
 * @warning Do not call any other LED APIs before calling this function.
 * @thread_safety No
 * @example Usage demo:
 * @code
 * // Create LED indicator instance
 * led_base_t *led = led_indicator_create("/dev/100ask_led0");
 * if (led == NULL) {
 *     // Handle creation failure
 * }
 * @endcode
 */
led_base_t *led_indicator_create(const char *dev_path);

/**
 * @brief   Destroy LED indicator instance
 * @details Release all resources: deinitialize hardware, close device file, free memory.
 * @param   base    Pointer to LED base class handle (returned by led_indicator_create).
 *                  NULL input is safely ignored.
 * @return  void
 *
 * @pre     Instance must be created by led_indicator_create.
 * @post    All resources are released, handle is invalid and cannot be used again.
 * @note    This is the destructor, must be called at the end of usage.
 * @warning Do not access the handle after calling this function.
 * @thread_safety No
 */
void led_indicator_destroy(led_base_t *base);

#ifdef __cplusplus
}
#endif

#endif