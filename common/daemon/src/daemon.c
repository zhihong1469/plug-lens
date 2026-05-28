/* SPDX-License-Identifier: MIT */
/**
 * @file    daemon.c
 * @brief   i.MX6ULL裁剪Buildroot专用守护进程（100%避坑）
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "daemon.h"
#include "config_common.h"


int create_daemon(void)
{
    pid_t pid;

    // 1. 第一次fork：脱离终端，父进程退出
    pid = fork();
    if (pid < 0) {
        perror("fork1 failed");
        return -1;
    }
    if (pid > 0) {
        exit(0); // 父进程直接退出
    }

    // 2. 创建新会话，成为会话组长
    if (setsid() < 0) {
        perror("setsid failed");
        return -1;
    }

    // 3. 第二次fork：防止重新获取终端（标准规范）
    pid = fork();
    if (pid < 0) {
        perror("fork2 failed");
        return -1;
    }
    if (pid > 0) {
        exit(0);
    }

    // ===================== 避坑1：设置文件权限掩码 =====================
    umask(0);

    // ===================== 避坑2：固定工作目录（核心！） =====================
    if (chdir(DAEMON_WORK_DIR) < 0) {
        perror("chdir failed");
        return -1;
    }

    // 4. 重定向输入输出到空设备（关闭终端交互）
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    return 0;
}