/* SPDX-License-Identifier: MIT */
/**
 * @file    daemon.c
 * @brief   标准Linux守护进程实现（适配IMX6ULL，无依赖）
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "daemon.h"

int create_daemon(void)
{
    // 1. 第一次fork：父进程退出，脱离终端
    pid_t pid = fork();
    if (pid < 0)  return -1;
    if (pid > 0)  exit(0);

    // 2. 创建新会话，成为会话组长 → 彻底脱离终端
    setsid();

    // 3. 第二次fork：防止重新获取终端（标准规范）
    pid = fork();
    if (pid < 0)  return -1;
    if (pid > 0)  exit(0);

    // 4. 重定向输入/输出/错误到 /dev/null（不打印、不占用终端）
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }

    return 0;
}