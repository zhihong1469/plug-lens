/* SPDX-License-Identifier: MIT */
/**
 * @file    daemon.h
 * @brief   Linux 守护进程模块（产品级后台运行）
 * @author  Luo
 */
#ifndef DAEMON_H
#define DAEMON_H

// 创建守护进程（后台运行，脱离终端）
int create_daemon(void);

#endif