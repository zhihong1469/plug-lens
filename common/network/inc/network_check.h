/* SPDX-License-Identifier: MIT */
/**
 * @file    network_check.h
 * @brief   Pure C native network status check component
 * @details Core capabilities for plug-lens Vision AI terminal:
 *          1. Ethernet link detection via ioctl (physical cable status)
 *          2. Internet connectivity check via socket + DNS probe
 *          3. Non-blocking socket with timeout control
 *          4. No dependency on system shell commands (ping/ip)
 *          5. Lightweight and high-performance for embedded Linux
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. Target network interface: eth0
 *          2. All functions are thread-safe
 *          3. No dynamic memory allocation
 */
#ifndef __NETWORK_CHECK_H__
#define __NETWORK_CHECK_H__

#include "config_common.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Check physical link status of eth0 network interface
 * @return  true: Cable connected and interface active
 *          false: Cable disconnected or interface error
 *
 * @details Uses ioctl system call to read NIC flags
 * @pre     No preconditions, callable at any time
 * @thread_safety Yes
 */
bool NetCheck_GetEth0LinkStatus(void);

/**
 * @brief   Check public internet connectivity via Alidns server
 * @return  true: Internet accessible
 *          false: Network unreachable or connection timeout
 *
 * @details Non-blocking TCP connect to DNS port (53) with 3s timeout
 *          No shell command dependency, pure socket implementation
 * @pre     Network interface initialized and link up
 * @thread_safety Yes
 */
bool NetCheck_GetInternetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __NETWORK_CHECK_H__ */