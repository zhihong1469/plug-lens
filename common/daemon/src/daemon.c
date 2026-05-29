/* SPDX-License-Identifier: MIT */
/**
 * @file    daemon.c
 * @brief   Standard Linux daemon process implementation
 * @details Internal implementation for plug-lens Vision AI terminal:
 *          1. Adopts POSIX-compliant double-fork daemon mechanism
 *          2. Optimized for i.MX6ULL Buildroot embedded Linux platform
 *          3. Integrated production-grade reliability optimizations
 *          4. Complete terminal detachment and background silent operation
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
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

/**
 * @brief   Public API implementation for standard Linux daemon creation
 * @details Implements industrial double-fork logic with embedded system optimizations
 *          and critical reliability fixes for production deployment
 * @return  0 on success, -1 on system call failure
 *
 * Core execution flow:
 * 1. First fork: Detach from parent process and release terminal control
 * 2. setsid(): Create new session and become session leader
 * 3. Second fork: Prevent acquiring controlling terminal (POSIX standard)
 * 4. Reset file permission mask and set fixed working directory
 * 5. Redirect standard I/O to /dev/null to disable terminal interaction
 */
int create_daemon(void)
{
    pid_t pid;

    // First fork: Detach from terminal, parent process exits immediately
    pid = fork();
    if (pid < 0) {
        perror("fork1 failed");
        return -1;
    }
    if (pid > 0) {
        exit(0);
    }

    // Create new session to isolate from controlling terminal
    if (setsid() < 0) {
        perror("setsid failed");
        return -1;
    }

    // Second fork: Critical standard step to avoid reacquiring terminal control
    pid = fork();
    if (pid < 0) {
        perror("fork2 failed");
        return -1;
    }
    if (pid > 0) {
        exit(0);
    }

    // Production optimization: Reset file mode mask for full file permission control
    umask(0);

    // Critical embedded fix: Set fixed working directory to eliminate path dependency
    if (chdir(DAEMON_WORK_DIR) < 0) {
        perror("chdir failed");
        return -1;
    }

    // Redirect standard I/O streams to /dev/null (disable terminal input/output)
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    return 0;
}