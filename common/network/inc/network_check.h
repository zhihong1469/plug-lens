/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 LuoZhihong (相醉为友)
 * All rights reserved.
 *
 * @file network_check.h
 * @brief 纯C原生网络状态检测组件
 * @brief Pure C native network status check component
 * @details 1. 网卡链路检测: 基于ioctl, 判断网线是否插入
 *          2. 外网连通检测: 基于Socket+DNS探测, 非阻塞+超时控制
 *          完全不依赖 ping / ip 等系统shell命令
 */
#ifndef __NETWORK_CHECK_H__
#define __NETWORK_CHECK_H__

#include "config_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 检测eth0网卡硬件链路状态
 * @brief Check eth0 hardware link status
 * @return true: 网线已插入, 链路正常
 *         false: 网线未插 / 网卡异常
 */
bool NetCheck_GetEth0LinkStatus(void);

/**
 * @brief 检测外网连通性(能否访问公网DNS)
 * @brief Check internet connectivity via public DNS
 * @return true: 外网连通
 *         false: 网络不通 / 连接超时
 */
bool NetCheck_GetInternetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __NETWORK_CHECK_H__ */