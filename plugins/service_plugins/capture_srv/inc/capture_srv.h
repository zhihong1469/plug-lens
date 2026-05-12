#ifndef __CAPTURE_SRV_H
#define __CAPTURE_SRV_H

#include "service_base.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 【采集服务私有事件】（0x1000-0x1FFF）
// 仅采集服务发布/订阅的事件，其他服务不需要关心
// 按照ID分段规范分配，禁止与其他服务冲突
// ==========================================================================
typedef enum {
    EVENT_TYPE_CAP_BASE = 0x1000,
    EVENT_TYPE_CAP_FRAME_READY,   // 摄像头帧就绪通知（核心事件）
    EVENT_TYPE_CAP_START,         // 采集开始
    EVENT_TYPE_CAP_STOP,          // 采集停止
    EVENT_TYPE_CAP_ERROR,         // 采集错误
    EVENT_TYPE_CAP_FPS_REPORT,    // 帧率统计上报

    EVENT_TYPE_CAP_MAX = 0x1FFF   // 采集事件上限
} cap_event_type_t;

/**
 * @brief  获取采集服务基类指针（对外唯一接口）
 * @return 服务基类指针
 */
service_base_t *capture_srv_get_instance(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAPTURE_SRV_H */