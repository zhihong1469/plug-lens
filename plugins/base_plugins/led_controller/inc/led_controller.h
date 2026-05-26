#ifndef __LED_CONTROLLER_H__
#define __LED_CONTROLLER_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED组件错误码枚举（项目统一规范）
 */
typedef enum {
    LED_OK                  = 0,    /* 成功 */
    LED_ERROR_INVALID_PARAM = -1,   /* 无效参数 */
    LED_ERROR_OPEN_DEV      = -2,   /* 打开设备失败 */
    LED_ERROR_WRITE_DEV     = -3,   /* 写入设备失败 */
    LED_ERROR_NOT_OPEN      = -4,   /* 设备未打开 */
} LedError_t;

/**
 * @brief LED控制器不透明指针（隐藏内部结构体实现）
 */
typedef struct LedController LedController_t;

/**
 * @brief  创建LED控制器实例
 * @retval 实例指针 / NULL(内存不足)
 */
LedController_t* LedController_Create(void);

/**
 * @brief  销毁LED控制器实例，释放资源
 * @param  self: 控制器实例指针
 */
void LedController_Destroy(LedController_t* self);

/**
 * @brief  打开LED设备节点
 * @param  self: 控制器实例
 * @param  dev_path: 设备路径(如: /dev/100ask_led0)
 * @retval LedError_t 错误码
 */
LedError_t LedController_Open(LedController_t* self, const char* dev_path);

/**
 * @brief  关闭LED设备节点
 * @param  self: 控制器实例
 */
void LedController_Close(LedController_t* self);

/**
 * @brief  点亮LED
 * @param  self: 控制器实例
 * @retval LedError_t 错误码
 */
LedError_t LedController_TurnOn(LedController_t* self);

/**
 * @brief  熄灭LED
 * @param  self: 控制器实例
 * @retval LedError_t 错误码
 */
LedError_t LedController_TurnOff(LedController_t* self);

/**
 * @brief  获取当前LED状态
 * @param  self: 控制器实例
 * @retval true=亮, false=灭
 */
bool LedController_GetState(const LedController_t* self);

#ifdef __cplusplus
}
#endif

#endif /* __LED_CONTROLLER_H__ */