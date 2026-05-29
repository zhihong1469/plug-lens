/* SPDX-License-Identifier: MIT */
#ifndef __LED_BASE_H__
#define __LED_BASE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file    led_base.h
 * @brief   LED Device Universal Abstract Base Class
 * @details C-OOP polymorphic design for LED devices,
 *          All LED subclasses (GPIO/PWM etc.) MUST inherit this base class.
 * @author  LuoZhihong
 */

/**
 * @brief LED universal state (Reuse project global state machine)
 */
typedef enum {
    LED_STATE_IDLE    = 0,    /**< Idle state, uninitialized */
    LED_STATE_INIT    = 1,    /**< Initialized state */
    LED_STATE_RUNNING = 2,    /**< Running state */
    LED_STATE_ERROR   = 5,    /**< Error state */
} led_state_t;

/**
 * @brief LED operation mode
 */
typedef enum {
    LED_MODE_OFF    = 0,      /**< LED Off */
    LED_MODE_ON     = 1,      /**< LED On */
    LED_MODE_BLINK  = 2,      /**< LED Blink */
} led_mode_t;

/**
 * @brief Forward declaration: Opaque pointer for LED base class
 */
typedef struct led_base led_base_t;

/**
 * @brief LED subclass virtual function table
 * @details Hardware-related implementation completed by subclasses
 */
typedef struct {
    /** Initialize LED hardware */
    int  (*init)(led_base_t *base);
    /** De-initialize LED hardware */
    int  (*deinit)(led_base_t *base);
    /** Set LED working mode */
    int  (*set_state)(led_base_t *base, led_mode_t mode);
    /** Get current LED working mode */
    int  (*get_state)(led_base_t *base, led_mode_t *mode);
} led_ops_t;

/**
 * @brief LED base class structure
 * @note All LED subclasses MUST take this as the FIRST member
 */
struct led_base {
    const led_ops_t   *ops;        /**< Virtual function table (polymorphic entry) */
    led_state_t        state;      /**< LED device state machine */
    const char        *name;       /**< LED device name */
    led_mode_t         current_mode; /**< Current working mode */
};

/************************* Base Class Public Interfaces *************************/
/**
 * @brief  Initialize LED base class (call subclass init)
 * @param  self  LED base class pointer
 * @return 0=success, negative=error code
 */
int led_base_init(led_base_t *self);

/**
 * @brief  De-initialize LED base class (call subclass deinit)
 * @param  self  LED base class pointer
 * @return 0=success, negative=error code
 */
int led_base_deinit(led_base_t *self);

/**
 * @brief  Turn LED on
 * @param  self  LED base class pointer
 * @return 0=success, negative=error code
 */
int led_base_turn_on(led_base_t *self);

/**
 * @brief  Turn LED off
 * @param  self  LED base class pointer
 * @return 0=success, negative=error code
 */
int led_base_turn_off(led_base_t *self);

/**
 * @brief  Set LED to blink mode
 * @param  self  LED base class pointer
 * @return 0=success, negative=error code
 */
int led_base_set_blink(led_base_t *self);

/**
 * @brief  Get current LED working mode
 * @param  self  LED base class pointer
 * @param  mode  Output: current mode
 * @return 0=success, negative=error code
 */
int led_base_get_mode(led_base_t *self, led_mode_t *mode);

/**
 * @brief  Get LED device state machine
 * @param  self  LED base class pointer
 * @return Current LED state
 */
led_state_t led_base_get_state(led_base_t *self);

#ifdef __cplusplus
}
#endif

#endif