/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 LuoZhihong (相醉为友)
 * All rights reserved.
 *
 * @file sys_time_sync.h
 * @brief 系统时区 + NTP网络时间同步组件
 * @brief System timezone and NTP time sync component
 * @note 依赖系统 ntpdate 工具(嵌入式Linux标配, 极简系统可预装)
 */
#ifndef __SYS_TIME_SYNC_H__
#define __SYS_TIME_SYNC_H__

#include "config_common.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// 配置
#define NTP_SERVER_ADDR       "ntp.aliyun.com" // 203.107.6.88
#define TIME_FORMAT_BUF_LEN   64

/**
 * @brief 设置系统时区为东八区(北京时间 CST)
 * @return true: 成功  false: 失败
 */
bool TimeSync_SetCstTimezone(void);

/**
 * @brief 通过NTP服务器同步系统时间
 * @param ntp_server: 自定义NTP服务器, 传NULL使用默认配置
 * @return true: 同步成功  false: 网络/命令执行失败
 */
bool TimeSync_NtpSync(const char *ntp_server);

/**
 * @brief 获取格式化本地时间字符串
 * @param buf: 外部传入缓冲区
 * @param buf_len: 缓冲区长度
 * @return true: 获取成功
 */
bool TimeSync_GetLocalTimeStr(char *buf, int buf_len);

/**
 * @brief 获取系统时间戳(秒)
 */
time_t TimeSync_GetTimeStamp(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYS_TIME_SYNC_H__ */