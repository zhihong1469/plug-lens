/* SPDX-License-Identifier: MIT */
/**
 * @file    led_base.c
 * @brief   LED Abstract Base Class Implementation
 * @details Internal implementation:
 *          - C-OOP polymorphic interface dispatch
 *          - State machine management for LED lifecycle
 *          - Pure abstract logic, no hardware-specific code
 *          - Unified error handling and parameter validation
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */

#include "led_base.h"
#include <stdio.h>
#include <errno.h>

/**
 * @brief   Validate LED base class instance pointer
 * @param   self  LED base class instance pointer
 * @return  true = valid instance (non-null and with ops table)
 *          false = invalid instance
 *
 * @note    Internal helper function for parameter check
 */
static inline bool led_base_is_valid(led_base_t *self)
{
    return self && self->ops;
}

/**
 * @brief   Public API: Initialize LED base class
 * @details Validates instance, checks state, calls subclass init
 */
int led_base_init(led_base_t *self)
{
    if (!led_base_is_valid(self))
        return -EINVAL;

    if (self->state != LED_STATE_IDLE)
        return -EBUSY;

    int ret = 0;
    if (self->ops->init)
        ret = self->ops->init(self);

    /* Update state machine based on initialization result */
    self->state = (ret == 0) ? LED_STATE_INIT : LED_STATE_ERROR;
    return ret;
}

/**
 * @brief   Public API: De-initialize LED base class
 * @details Calls subclass deinit and resets state/mode
 */
int led_base_deinit(led_base_t *self)
{
    if (!led_base_is_valid(self))
        return -EINVAL;

    int ret = 0;
    if (self->ops->deinit)
        ret = self->ops->deinit(self);

    /* Reset to default idle state */
    self->state = LED_STATE_IDLE;
    self->current_mode = LED_MODE_OFF;
    return ret;
}

/**
 * @brief   Public API: Set LED to ON mode
 * @details Dispatch to subclass set_state and update local mode
 */
int led_base_turn_on(led_base_t *self)
{
    if (!led_base_is_valid(self))
        return -EINVAL;

    int ret = 0;
    if (self->ops->set_state)
        ret = self->ops->set_state(self, LED_MODE_ON);

    if (ret == 0)
        self->current_mode = LED_MODE_ON;

    return ret;
}

/**
 * @brief   Public API: Set LED to OFF mode
 */
int led_base_turn_off(led_base_t *self)
{
    if (!led_base_is_valid(self))
        return -EINVAL;

    int ret = 0;
    if (self->ops->set_state)
        ret = self->ops->set_state(self, LED_MODE_OFF);

    if (ret == 0)
        self->current_mode = LED_MODE_OFF;

    return ret;
}

/**
 * @brief   Public API: Set LED to BLINK mode
 */
int led_base_set_blink(led_base_t *self)
{
    if (!led_base_is_valid(self))
        return -EINVAL;

    int ret = 0;
    if (self->ops->set_state)
        ret = self->ops->set_state(self, LED_MODE_BLINK);

    if (ret == 0)
        self->current_mode = LED_MODE_BLINK;

    return ret;
}

/**
 * @brief   Public API: Get current LED working mode
 */
int led_base_get_mode(led_base_t *self, led_mode_t *mode)
{
    if (!led_base_is_valid(self) || !mode)
        return -EINVAL;

    *mode = self->current_mode;
    return 0;
}

/**
 * @brief   Public API: Get LED state machine status
 */
led_state_t led_base_get_state(led_base_t *self)
{
    if (!self)
        return LED_STATE_ERROR;
    return self->state;
}