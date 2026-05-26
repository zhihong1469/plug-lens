#ifndef __LED_BASE_H__
#define __LED_BASE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED 统一状态（复用项目全局状态）
 */
typedef enum {
    LED_STATE_IDLE    = 0,
    LED_STATE_INIT    = 1,
    LED_STATE_RUNNING = 2,
    LED_STATE_ERROR   = 5,
} led_state_t;

/**
 * @brief LED 操作模式
 */
typedef enum {
    LED_MODE_OFF    = 0,
    LED_MODE_ON     = 1,
    LED_MODE_BLINK  = 2,
} led_mode_t;

/**
 * @brief 前向声明：LED基类不透明指针
 */
typedef struct led_base led_base_t;

/**
 * @brief LED 子类虚函数表（硬件相关实现由子类完成）
 */
typedef struct {
    int  (*init)(led_base_t *base);
    int  (*deinit)(led_base_t *base);
    int  (*set_state)(led_base_t *base, led_mode_t mode);
    int  (*get_state)(led_base_t *base, led_mode_t *mode);
} led_ops_t;

/**
 * @brief LED 基类结构体（所有LED子类第一个成员必须是它）
 */
struct led_base {
    const led_ops_t   *ops;
    led_state_t        state;
    const char        *name;
    led_mode_t         current_mode;
};

/************************* 基类公共接口 *************************/
int led_base_init(led_base_t *self);
int led_base_deinit(led_base_t *self);
int led_base_turn_on(led_base_t *self);
int led_base_turn_off(led_base_t *self);
int led_base_set_blink(led_base_t *self);
int led_base_get_mode(led_base_t *self, led_mode_t *mode);
led_state_t led_base_get_state(led_base_t *self);

#ifdef __cplusplus
}
#endif

#endif