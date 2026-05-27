/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 LuoZhihong (相醉为友)
 * All rights reserved.
 *
 * @file sys_time_sync.c
 * @brief 系统时间同步实现（纯组件，无内部日志打印）
 */
#include "sys_time_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CMD_BUF_LEN  128

bool TimeSync_SetCstTimezone(void)
{
    // 仅执行命令，仅返回成功/失败，无内部打印
    int ret = system("ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime");
    return (ret == 0);
}

bool TimeSync_NtpSync(const char *ntp_server)
{
    char cmd[CMD_BUF_LEN] = {0};
    const char *server = ntp_server ? ntp_server : NTP_SERVER_ADDR;

    // 嵌入式Linux专用：强制时间同步（-u 绕过端口占用）
    snprintf(cmd, sizeof(cmd) - 1, "ntpdate -s -u %s", server);
    int ret = system(cmd);

    return (ret == 0);
}
bool TimeSync_GetLocalTimeStr(char *buf, int buf_len)
{
    if (buf == NULL || buf_len < 16)
    {
        return false;
    }

    time_t now = time(NULL);
    struct tm local_tm;
    localtime_r(&now, &local_tm);

    // 格式化时间：2025-01-01 12:00:00
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

time_t TimeSync_GetTimeStamp(void)
{
    return time(NULL);
}