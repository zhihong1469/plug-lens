/* SPDX-License-Identifier: MIT */
/**
 * @file    daemon.h
 * @brief   守护进程接口头文件
 */
#ifndef DAEMON_H
#define DAEMON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  创建标准Linux守护进程
 * @return 0成功，-1失败
 */
int create_daemon(void);

#ifdef __cplusplus
}
#endif

#endif // DAEMON_H