/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 LuoZhihong
 * All rights reserved.
 */
/**
 * @file    sys_time_sync.h
 * @brief   System timezone and NTP network time synchronization component
 * @details Core functions:
 *          1. Set system timezone to UTC+8 (CST/Beijing Time)
 *          2. Synchronize system time via NTP server
 *          3. Get formatted local time string and timestamp
 * @note    Depends on system ntpdate tool (pre-installed in embedded Linux)
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#ifndef __SYS_TIME_SYNC_H__
#define __SYS_TIME_SYNC_H__

#include "config_common.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===================== Configuration Macros =====================
/** NTP server address (Alibaba Cloud NTP) */
#define NTP_SERVER_ADDR       "ntp.aliyun.com"
/** Buffer length for formatted time string */
#define TIME_FORMAT_BUF_LEN   64

// ===================== Public API Declarations =====================
/**
 * @brief   Set system timezone to UTC+8 (CST, Beijing Time)
 * @return  true: Success, false: Failure
 */
bool TimeSync_SetCstTimezone(void);

/**
 * @brief   Synchronize system time via NTP server
 * @param   ntp_server  Custom NTP server address, NULL to use default
 * @return  true: Sync success, false: Network/command failure
 */
bool TimeSync_NtpSync(const char *ntp_server);

/**
 * @brief   Get formatted local time string (YYYY-MM-DD HH:MM:SS)
 * @param   buf         External buffer for time string
 * @param   buf_len     Length of the buffer
 * @return  true: Success, false: Invalid parameter
 */
bool TimeSync_GetLocalTimeStr(char *buf, int buf_len);

/**
 * @brief   Get system timestamp (seconds since epoch)
 * @return  time_t  Unix timestamp
 */
time_t TimeSync_GetTimeStamp(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYS_TIME_SYNC_H__ */