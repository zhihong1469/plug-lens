/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 LuoZhihong (相醉为友)
 * All rights reserved.
 *
 * @file network_check.c
 * @brief 纯C原生网络状态检测实现
 */
#include "network_check.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/select.h>
#include <bits/socket.h>

// 配置（和头文件对应）
#define NET_DEV_NAME     "eth0"
#define NET_DNS_SERVER   "223.5.5.5"    // 阿里DNS（稳定）
#define NET_DNS_PORT     53
#define NET_CHECK_TIMEOUT 3             // 超时3秒

static int sock_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool NetCheck_GetEth0LinkStatus(void)
{
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) return false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, NET_DEV_NAME, IFNAMSIZ - 1);

    int ret = ioctl(sock_fd, SIOCGIFFLAGS, &ifr);
    close(sock_fd);

    if (ret < 0) return false;
    // 网卡启用 + 物理网线连接
    return ((ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING));
}

bool NetCheck_GetInternetStatus(void)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) return false;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_port        = htons(NET_DNS_PORT);
    inet_pton(AF_INET, NET_DNS_SERVER, &serv_addr.sin_addr);

    sock_set_nonblock(sock_fd);
    int conn_ret = connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    bool net_ok = false;
    if (conn_ret == 0) {
        net_ok = true;
    } else if (errno == EINPROGRESS) {
        fd_set wfds;
        struct timeval tv;
        FD_ZERO(&wfds);
        FD_SET(sock_fd, &wfds);

        tv.tv_sec  = NET_CHECK_TIMEOUT;
        tv.tv_usec = 0;

        int sel_ret = select(sock_fd + 1, NULL, &wfds, NULL, &tv);
        if (sel_ret > 0) {
            // ✅ 修复关键BUG：必须校验socket是否真正连接成功
            int error = 0;
            socklen_t len = sizeof(error);
            getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &len);
            if (error == 0) net_ok = true;
        }
    }

    close(sock_fd);
    return net_ok;
}