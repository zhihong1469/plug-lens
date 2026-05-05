#ifndef FACE_DETECT_SRV_H
#define FACE_DETECT_SRV_H

#include <stdint.h>
#include <stdbool.h>
#include "module_fsm.h"
#include "event_bus.h"
#include "data_bus.h"
#include "ai_model_link.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 配置结构体
// ==========================================================================
typedef struct {
    // AI模型配置
    const char* model_path;
    int ai_input_w;
    int ai_input_h;
    float score_threshold;
    float iou_threshold;

    // 总线句柄
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;

    // 回调（给全局FSM）
    struct {
        void (*state_change_cb)(const char* module_name,
                                 module_state_t old_state,
                                 module_state_t new_state,
                                 void* user_data);
        void* user_data;
    } callbacks;

    // 自动启动
    bool auto_start;
} face_detect_srv_config_t;

// ==========================================================================
// 句柄定义
// ==========================================================================
typedef void* face_detect_srv_handle_t;

// ==========================================================================
// 对外API
// ==========================================================================

/**
 * @brief 创建人脸检测服务
 */
int face_detect_srv_create(const face_detect_srv_config_t* config,
                           face_detect_srv_handle_t* out_handle);

/**
 * @brief 获取子状态机句柄（用于注册到全局FSM）
 */
module_fsm_handle_t face_detect_srv_get_fsm(face_detect_srv_handle_t handle);

/**
 * @brief 销毁人脸检测服务
 */
int face_detect_srv_destroy(face_detect_srv_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // FACE_DETECT_SRV_H