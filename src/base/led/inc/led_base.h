/* SPDX-License-Identifier: MIT */

/**
 * @file    led_base.h
 * @brief   LED Device Universal Abstract Base Class
 * @details C-OOP polymorphic design for LED devices, supports GPIO/PWM/other hardware implementations.
 *          All LED subclasses MUST inherit this base class, provides unified public interface.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All functions are not thread-safe unless marked specially.
 *          2. Call functions in order: init → operation → deinit.
 *          3. Subclasses must place led_base_t as the first member.
 */

/**
 * @brief   LED universal state machine
 * @details Defines all lifecycle states of the LED base class
 */

#ifndef __LED_BASE_H__
#define __LED_BASE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATE_IDLE    = 0,    /**< Idle state, uninitialized, default state */
    LED_STATE_INIT    = 1,    /**< Initialized state, ready to work */
    LED_STATE_RUNNING = 2,    /**< Running state, LED in normal operation */
    LED_STATE_ERROR   = 5,    /**< Error state, hardware or logic failure */
} led_state_t;

/**
 * @brief   LED operation mode
 * @details All supported working modes for LED devices
 */
typedef enum {
    LED_MODE_OFF    = 0,      /**< LED power off */
    LED_MODE_ON     = 1,      /**< LED steady on */
    LED_MODE_BLINK  = 2,      /**< LED periodic blink */
} led_mode_t;

/**
 * @brief   Forward declaration of LED base class
 * @details Opaque structure, external modules only use the pointer
 */
typedef struct led_base led_base_t;

/**
 * @brief   LED subclass virtual function table
 * @details Hardware-specific implementation callbacks, must be implemented by subclasses
 * @note    All callbacks are optional, NULL is allowed if unused
 */
typedef struct {
    int  (*init)(led_base_t *base);      /**< Subclass hardware initialization */
    int  (*deinit)(led_base_t *base);    /**< Subclass hardware de-initialization */
    int  (*set_state)(led_base_t *base, led_mode_t mode); /**< Set LED working mode */
    int  (*get_state)(led_base_t *base, led_mode_t *mode); /**< Get LED working mode */
} led_ops_t;

/**
 * @brief   LED base class core structure
 * @details Base object for all LED devices, supports polymorphic dispatch
 * @note    1. Do not modify members directly, use public APIs only
 *          2. Subclasses must place this structure as the first member
 */
struct led_base {
    const led_ops_t   *ops;        /**< Virtual function table for polymorphic calls */
    led_state_t        state;      /**< LED state machine */
    const char        *name;       /**< LED device identifier name */
    led_mode_t         current_mode; /**< Current working mode */
};

/************************* Public Interface Functions *************************/
/**
 * @brief   Initialize the LED base class
 * @param   self    Pointer to LED base class instance, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be in IDLE state
 * @post    Instance transitions to INIT state if success, ERROR state if failure
 *
 * @note    Calls subclass initialization function automatically
 * @warning Do not call this function repeatedly
 * @thread_safety No
 *
 * @example
 * @code
 * led_base_t *led = led_instance_create();
 * int ret = led_base_init(led);
 * if (ret != 0) {
 *     // Handle initialization error
 * }
 * @endcode
 */
int led_base_init(led_base_t *self);

/**
 * @brief   De-initialize the LED base class
 * @param   self    Pointer to LED base class instance, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be initialized
 * @post    Instance resets to IDLE state, mode set to OFF
 *
 * @note    Calls subclass de-initialization function automatically
 * @thread_safety No
 */
int led_base_deinit(led_base_t *self);

/**
 * @brief   Turn LED to steady ON mode
 * @param   self    Pointer to LED base class instance, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be initialized
 * @post    Current mode updated to LED_MODE_ON
 *
 * @note    Forwards request to subclass set_state callback
 * @thread_safety No
 */
int led_base_turn_on(led_base_t *self);

/**
 * @brief   Turn LED to OFF mode
 * @param   self    Pointer to LED base class instance, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be initialized
 * @post    Current mode updated to LED_MODE_OFF
 *
 * @thread_safety No
 */
int led_base_turn_off(led_base_t *self);

/**
 * @brief   Set LED to BLINK mode
 * @param   self    Pointer to LED base class instance, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be initialized
 * @post    Current mode updated to LED_MODE_BLINK
 *
 * @thread_safety No
 */
int led_base_set_blink(led_base_t *self);

/**
 * @brief   Get current LED working mode
 * @param   self    Pointer to LED base class instance, cannot be NULL
 * @param   mode    Output pointer to store current mode, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be valid
 * @post    Output parameter filled with current mode
 *
 * @thread_safety No
 */
int led_base_get_mode(led_base_t *self, led_mode_t *mode);

/**
 * @brief   Get current LED state machine status
 * @param   self    Pointer to LED base class instance
 * @return  Current led_state_t value, LED_STATE_ERROR if NULL pointer
 *
 * @thread_safety No
 */
led_state_t led_base_get_state(led_base_t *self);

#ifdef __cplusplus
}
#endif

#endif