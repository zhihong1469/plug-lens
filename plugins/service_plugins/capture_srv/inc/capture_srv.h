#ifndef __CAPTURE_SRV_H
#define __CAPTURE_SRV_H

#include "service_base.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 采集服务 私有事件定义（0x1000~0x1FFF）
// ==========================================================================
typedef enum {
    EVENT_TYPE_CAP_BASE = 0x1000,
    EVENT_TYPE_CAP_FRAME_READY,   // 帧就绪通知
    EVENT_TYPE_CAP_START,         // 采集开始
    EVENT_TYPE_CAP_STOP,          // 采集停止
    EVENT_TYPE_CAP_ERROR,         // 采集异常
    EVENT_TYPE_CAP_FPS_REPORT,    // 帧率上报

    EVENT_TYPE_CAP_MAX = 0x1FFF
} cap_event_type_t;

// ==========================================================================
// 对外唯一接口：获取采集服务基类指针
// ==========================================================================
service_base_t *capture_srv_get_instance(void);

#ifdef __cplusplus
}
#endif

#endif // __CAPTURE_SRV_H