/**
 * @file    led_indicator.c
 * @brief   LED Indicator Subclass Driver Implementation
 * @details Internal implementation for hardware LED indicator:
 *          - Inherits from led_base_t (OOP inheritance pattern).
 *          - Controls LED via Linux character device read/write operations.
 *          - Implements mandatory virtual functions: init, deinit, set_state.
 *          - Static memory allocation only at creation phase.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "led_indicator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/**
 * @brief   Private structure for LED indicator instance
 * @details Opaque subclass structure extending the LED base class.
 *          Base class member MUST be placed at the first position for inheritance.
 * @note    Do not access members directly; use public LED base APIs only.
 */
typedef struct {
    led_base_t      base;       /**< LED base class (MUST be first member) */
    const char     *dev_path;   /**< Linux LED device node path */
    int             fd;         /**< Device file descriptor, -1 = closed */
} led_indicator_t;

/* ============================================================================
 * Private Helper Functions
 * ========================================================================== */
/**
 * @brief   Write LED state to hardware device
 * @details Low-level write operation for LED control via file descriptor.
 * @param   me      Pointer to LED indicator private instance
 * @param   state   LED output state (0 = OFF, 1 = ON)
 * @return  0 on success; negative error code on failure
 *          -ENODEV: Invalid file descriptor
 *          -EIO: Write operation failed
 */
static int led_indicator_write(led_indicator_t *me, uint8_t state)
{
    if (me->fd < 0) {
        return -ENODEV;
    }

    ssize_t ret = write(me->fd, &state, 1);
    if (ret != 1) {
        perror("[LED IND] write failed");
        return -EIO;
    }
    return 0;
}

/* ============================================================================
 * Subclass Virtual Function Implementations (Inherit from led_base)
 * ========================================================================== */
/**
 * @brief   Initialize LED indicator hardware
 * @details Open device file and set default LED state to OFF.
 * @param   base    Pointer to LED base class handle
 * @return  0 on success; negative errno code on failure
 *
 * @pre     Instance must be created by led_indicator_create
 * @post    Device opened, LED turned OFF, state changes to INITIALIZED
 */
static int led_indicator_init(led_base_t *base)
{
    led_indicator_t *me = (led_indicator_t *)base;

    // Open LED hardware device node
    me->fd = open(me->dev_path, O_RDWR);
    if (me->fd < 0) {
        perror("[LED IND] open failed");
        return -errno;
    }

    // Set default state: LED OFF
    int ret = led_indicator_write(me, 0);
    if (ret < 0) {
        close(me->fd);
        me->fd = -1;
        return ret;
    }

    printf("[LED IND] Initialization completed ✅\n");
    return 0;
}

/**
 * @brief   De-initialize LED indicator hardware
 * @details Turn off LED and close device file to release resources.
 * @param   base    Pointer to LED base class handle
 * @return  0 always (success)
 *
 * @pre     Instance must be initialized
 * @post    Device closed, LED turned OFF, file descriptor reset to -1
 */
static int led_indicator_deinit(led_base_t *base)
{
    led_indicator_t *me = (led_indicator_t *)base;

    if (me->fd >= 0) {
        led_indicator_write(me, 0);
        close(me->fd);
        me->fd = -1;
    }

    printf("[LED IND] De-initialization completed\n");
    return 0;
}

/**
 * @brief   Set LED working mode
 * @details Support ON/OFF modes; BLINK mode is not supported in this driver.
 * @param   base    Pointer to LED base class handle
 * @param   mode    Target LED mode (ON/OFF/BLINK)
 * @return  0 on success; negative error code on failure
 *          -ENOTSUP: Blink mode not supported
 *          -EINVAL: Invalid input mode
 *
 * @pre     Instance must be initialized successfully
 * @post    LED physical state matches the input mode
 * @note    Blink mode requires timer extension for future implementation
 */
static int led_indicator_set_state(led_base_t *base, led_mode_t mode)
{
    led_indicator_t *me = (led_indicator_t *)base;
    uint8_t val = 0;

    switch (mode) {
        case LED_MODE_ON:
            val = 1;
            break;
        case LED_MODE_OFF:
            val = 0;
            break;
        case LED_MODE_BLINK:
            // Extend with timer for blink functionality
            printf("[LED IND] Blink mode is not supported\n");
            return -ENOTSUP;
        default:
            return -EINVAL;
    }

    return led_indicator_write(me, val);
}

/* ============================================================================
 * LED Subclass Virtual Function Table (CONST required)
 * ========================================================================== */
/**
 * @brief   Virtual function table for LED indicator subclass
 * @details Implements all mandatory operations from led_base_t interface.
 *          get_state is unimplemented (NULL) as it is not required.
 */
static const led_ops_t g_led_indicator_ops = {
    .init      = led_indicator_init,
    .deinit    = led_indicator_deinit,
    .set_state = led_indicator_set_state,
    .get_state = NULL,   /**< Unimplemented interface */
};

/* ============================================================================
 * Public Constructor & Destructor Functions
 * ========================================================================== */
/**
 * @brief   Create LED indicator instance (public constructor)
 * @copydoc led_indicator_create
 */
led_base_t *led_indicator_create(const char *dev_path)
{
    if (!dev_path) {
        return NULL;
    }

    // Allocate memory for subclass instance
    led_indicator_t *me = (led_indicator_t *)calloc(1, sizeof(*me));
    if (!me) {
        printf("[LED IND] Memory allocation failed\n");
        return NULL;
    }

    // Initialize base class properties (OOP inheritance core logic)
    me->base.ops   = &g_led_indicator_ops;
    me->base.name  = "led_indicator";
    me->base.state = LED_STATE_IDLE;
    me->base.current_mode = LED_MODE_OFF;

    // Initialize private parameters
    me->dev_path = dev_path;
    me->fd       = -1;

    printf("[LED IND] Instance created successfully\n");
    return &me->base;
}

/**
 * @brief   Destroy LED indicator instance (public destructor)
 * @copydoc led_indicator_destroy
 */
void led_indicator_destroy(led_base_t *base)
{
    if (!base) {
        return;
    }

    led_indicator_t *me = (led_indicator_t *)base;

    // Release hardware and system resources
    led_base_deinit(base);
    free(me);

    printf("[LED IND] Instance destroyed successfully\n");
}