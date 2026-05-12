#ifndef FACE_DETECT_SRV_H
#define FACE_DETECT_SRV_H

#include "event_bus.h"
#include "data_bus.h"
#include "ai_model_base.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 服务不透明句柄（外部仅用指针操作，OOP封装）
typedef struct face_detect_srv face_detect_srv_handle_t;

// 服务配置（对接全局配置）
typedef struct {
    // 总线句柄（外部传入，无全局变量）
    event_bus_handle_t  evt_bus;
    data_bus_handle_t   data_bus;
    // AI模型配置
    ai_model_config_t   ai_cfg;
} face_detect_cfg_t;

// 服务内部状态（私有，对外仅用于调试）
typedef enum {
    FACE_SRV_STATE_IDLE = 0,
    FACE_SRV_STATE_INIT,
    FACE_SRV_STATE_RUNNING,
    FACE_SRV_STATE_PAUSED,
    FACE_SRV_STATE_STOPPED,
    FACE_SRV_STATE_ERROR
} face_srv_state_t;

// ==============================================
// 对外核心接口
// ==============================================
/**
 * @brief 创建人脸检测服务
 * @param cfg 配置（双总线句柄+AI配置）
 * @return 服务句柄
 */
face_detect_srv_handle_t *face_detect_srv_create(const face_detect_cfg_t *cfg);

/**
 * @brief 启动服务
 */
int face_detect_srv_start(face_detect_srv_handle_t *srv);

/**
 * @brief 停止服务
 */
int face_detect_srv_stop(face_detect_srv_handle_t *srv);

/**
 * @brief 销毁服务
 */
void face_detect_srv_destroy(face_detect_srv_handle_t **srv);

/**
 * @brief 获取当前服务状态
 */
face_srv_state_t face_detect_srv_get_state(face_detect_srv_handle_t *srv);

#ifdef __cplusplus
}
#endif

#endif // FACE_DETECT_SRV_H