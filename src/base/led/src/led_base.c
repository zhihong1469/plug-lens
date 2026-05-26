#include "led_base.h"
#include <stdio.h>
#include <errno.h>

static inline bool led_base_is_valid(led_base_t *self)
{
    return self && self->ops;
}

int led_base_init(led_base_t *self)
{
    if (!led_base_is_valid(self))
        return -EINVAL;

    if (self->state != LED_STATE_IDLE)
        return -EBUSY;

    int ret = 0;
    if (self->ops->init)
        ret = self->ops->init(self);

    self->state = (ret == 0) ? LED_STATE_INIT : LED_STATE_ERROR;
    return ret;
}

int led_base_deinit(led_base_t *self)
{
    if (!led_base_is_valid(self))
        return -EINVAL;

    int ret = 0;
    if (self->ops->deinit)
        ret = self->ops->deinit(self);

    self->state = LED_STATE_IDLE;
    self->current_mode = LED_MODE_OFF;
    return ret;
}

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

int led_base_get_mode(led_base_t *self, led_mode_t *mode)
{
    if (!led_base_is_valid(self) || !mode)
        return -EINVAL;

    *mode = self->current_mode;
    return 0;
}

led_state_t led_base_get_state(led_base_t *self)
{
    if (!self)
        return LED_STATE_ERROR;
    return self->state;
}