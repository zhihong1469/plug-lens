#ifndef __CAPTURE_SRV_H
#define __CAPTURE_SRV_H

#include "service_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  获取采集服务基类指针（对外唯一接口）
 * @return 服务基类指针
 */
service_base_t *capture_srv_get_instance(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAPTURE_SRV_H */