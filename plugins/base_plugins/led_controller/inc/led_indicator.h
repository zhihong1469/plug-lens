#ifndef __LED_INDICATOR_H__
#define __LED_INDICATOR_H__

#include "led_base.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED指示灯子类创建接口
 * @param dev_path: LED设备节点路径 /dev/100ask_led0
 * @return led_base_t* 基类指针, NULL=失败
 */
led_base_t *led_indicator_create(const char *dev_path);

/**
 * @brief 销毁LED指示灯实例
 * @param base: LED基类指针
 */
void led_indicator_destroy(led_base_t *base);

#ifdef __cplusplus
}
#endif

#endif