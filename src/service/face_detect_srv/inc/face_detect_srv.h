#ifndef FACE_DETECT_SRV_H
#define FACE_DETECT_SRV_H

#include <stdint.h>
#include <stdbool.h>
#include "module_fsm.h"
#include "event_bus.h"
#include "data_bus.h"

// 不透明句柄
typedef void* face_detect_srv_handle_t;

// 回调函数类型
typedef void (*face_detect_state_cb_t)(const char *module_name,
                                       module_state_t old_state,
                                       module_state_t new_state,
                                       void *user_data);

// 回调集合
typedef struct {
    face_detect_state_cb_t state_change_cb;
    void *user_data;
} face_detect_srv_callbacks_t;

// 服务配置
typedef struct {
    event_bus_handle_t evt_bus;          // 事件总线句柄
    data_bus_handle_t data_bus;          // 数据总线句柄
    const char *model_path;              // AI模型路径
    int ai_input_w;                      // AI模型输入宽
    int ai_input_h;                      // AI模型输入高
    float score_threshold;               // 置信度阈值
    float iou_threshold;                 // IOU阈值
    bool auto_start;                     // 自动启动
    face_detect_srv_callbacks_t callbacks; // 回调
} face_detect_srv_config_t;

// 对外API
int face_detect_srv_create(const face_detect_srv_config_t *config,
                           face_detect_srv_handle_t *out_handle);
int face_detect_srv_destroy(face_detect_srv_handle_t handle);
module_fsm_handle_t face_detect_srv_get_fsm(face_detect_srv_handle_t handle);

#endif // FACE_DETECT_SRV_H