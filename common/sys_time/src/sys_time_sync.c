/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 LuoZhihong
 * All rights reserved.
 */
/**
 * @file    sys_time_sync.c
 * @brief   Implementation of system time synchronization component
 * @details Pure functional component, no internal log printing
 *          Uses system commands and standard C time library
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "sys_time_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** Buffer length for NTP command string */
#define CMD_BUF_LEN  128

/**
 * @brief   Set timezone to Asia/Shanghai (UTC+8)
 * @return  Operation result
 */
bool TimeSync_SetCstTimezone(void)
{
    // Create symbolic link for timezone configuration
    int ret = system("ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime");
    return (ret == 0);
}

/**
 * @brief   Execute NTP time synchronization
 * @param   ntp_server  NTP server domain/IP
 * @return  Sync result
 */
bool TimeSync_NtpSync(const char *ntp_server)
{
    char cmd[CMD_BUF_LEN] = {0};
    const char *server = ntp_server ? ntp_server : NTP_SERVER_ADDR;
    
    // Embedded Linux optimized ntpdate command: silent mode + udp port
    snprintf(cmd, sizeof(cmd) - 1, "ntpdate -s -u %s", server);
    int ret = system(cmd);
    return (ret == 0);
}

/**
 * @brief   Get formatted local time with thread-safe localtime_r
 * @param   buf         Output string buffer
 * @param   buf_len     Buffer size
 * @return  Operation result
 */
bool TimeSync_GetLocalTimeStr(char *buf, int buf_len)
{
    if (buf == NULL || buf_len < 16)
    {
        return false;
    }

    time_t now = time(NULL);
    struct tm local_tm;
    // Thread-safe local time conversion
    localtime_r(&now, &local_tm);

    // Format: YYYY-MM-DD HH:MM:SS
    snprintf(buf, buf_len,
             "%04d-%02d-%02d %02d:%02d:%02d",
             local_tm.tm_year + 1900,
             local_tm.tm_mon + 1,
             local_tm.tm_mday,
             local_tm.tm_hour,
             local_tm.tm_min,
             local_tm.tm_sec);

    return true;
}

/**
 * @brief   Get current system Unix timestamp
 * @return  Timestamp in seconds
 */
time_t TimeSync_GetTimeStamp(void)
{
    return time(NULL);
}