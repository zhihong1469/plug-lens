#ifndef __NET_PUSH_SRV_H
#define __NET_PUSH_SRV_H

#include <stdint.h>
#include <stdbool.h>
#include "data_bus.h"
#include "event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 网络推流配置
// ==========================================================================
typedef struct {
    const char*     bind_ip;        // 绑定IP（0.0.0.0=所有网卡）
    uint16_t        bind_port;      // 绑定端口（默认8888）
    uint32_t        max_clients;    // 最大客户端数（简易版默认1）
    uint32_t        queue_capacity; // 发送队列长度（默认2，丢旧保新）
    bool            enable_log;     // 是否开启推流日志

    // 总线句柄
    data_bus_handle_t   data_bus;
    event_bus_handle_t  event_bus;
} net_push_srv_config_t;

// 不透明服务句柄
typedef void* net_push_srv_handle_t;

// ==========================================================================
// 对外核心API
// ==========================================================================

/**
 * @brief 创建网络推流服务
 * @param config 配置参数
 * @param out_handle 输出服务句柄
 * @return 0成功，负数失败
 */
int net_push_srv_create(const net_push_srv_config_t* config,
                        net_push_srv_handle_t* out_handle);

/**
 * @brief 启动推流服务
 * @param handle 服务句柄
 * @return 0成功
 */
int net_push_srv_start(net_push_srv_handle_t handle);

/**
 * @brief 停止推流服务
 * @param handle 服务句柄
 * @return 0成功
 */
int net_push_srv_stop(net_push_srv_handle_t handle);

/**
 * @brief 销毁推流服务，释放所有资源
 * @param handle 服务句柄
 * @return 0成功
 */
int net_push_srv_destroy(net_push_srv_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // __NET_PUSH_SRV_H