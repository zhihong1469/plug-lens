/* SPDX-License-Identifier: MIT */
#ifndef __FACE_DETECT_SRV_H
#define __FACE_DETECT_SRV_H

#include "service_base.h"
#include "ai_model_base.h"
#include "ai_model_mnn.hpp"
#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 人脸检测服务 私有事件定义（0x2000~0x2FFF）
// ==========================================================================
typedef enum {
    EVENT_TYPE_FACE_BASE = 0x2000,
    EVENT_TYPE_FACE_READY,         // 服务就绪
    EVENT_TYPE_FACE_RUNNING,       // 运行中
    EVENT_TYPE_FACE_STOPPED,       // 已停止
    EVENT_TYPE_FACE_RESULT,        // 检测结果通知
    EVENT_TYPE_FACE_ERROR,         // 异常

    EVENT_TYPE_FACE_MAX = 0x2FFF
} face_event_type_t;

// ==========================================================================
// 对外唯一接口：获取人脸检测服务基类指针
// ==========================================================================
service_base_t *face_detect_srv_get_instance(void);

#ifdef __cplusplus
}
#endif

#endif // __FACE_DETECT_SRV_H