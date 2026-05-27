/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 LuoZhihong (相醉为友)
 * All rights reserved.
 *
 * @file config_common.h
 * @brief 项目通用配置宏定义
 * @brief Common configuration macros for project
 * @note 网络、时间模块统一参数配置
 */
#ifndef __CONFIG_COMMON_H__
#define __CONFIG_COMMON_H__

#include <stdbool.h>
#ifndef USE_SD
#define USE_SD 1
#endif
// ==========================================================================
// 日志配置（工业级可配置）
// ==========================================================================
#define LOG_FILE_PATH                  "/mnt/sdcard/log/app.log"   // 工业级日志文件路径
#define LOG_MAX_FILE_SIZE (10 * 1024 * 1024)    // 10MB日志滚动阈值

// ==========================================================================
// SD配置（工业级可配置）
// ==========================================================================
#define CONFIG_SD_STORAGE_ROOT_PATH           "/mnt/sdcard"
#define CONFIG_SD_STORAGE_DIR                 "/mnt/sdcard/face_capture" // debug use "/mnt/sdcard/face_capture"/mnt/face_capture

// ===================== 功能总开关 =====================
#define USE_NET_CHECK      1    // 开启网络状态检测
#define USE_NET_TIME_SYNC  1    // 开启NTP网络时间同步

// ===================== 网络检测参数 =====================
#define NET_CHECK_TIMEOUT  2    // 网络检测超时(秒)
#define NET_DNS_SERVER     "114.114.114.114"  // 国内公共DNS
#define NET_DNS_PORT       53   // DNS标准端口
#define NET_DEV_NAME       "eth0"// 开发板网口设备名

// ===================== NTP时间同步参数 =====================
#define NTP_SERVER_ADDR    "time1.aliyun.com" // 阿里云NTP服务器

#endif /* __CONFIG_COMMON_H__ */