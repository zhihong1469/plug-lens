/* SPDX-License-Identifier: MIT */
/**
 * @file    network_check.c
 * @brief   Implementation of pure C network status detection
 * @details Low-level implementation:
 *          1. NIC link status: SIOCGIFFLAGS ioctl for eth0
 *          2. Internet check: Non-blocking TCP connect to public DNS
 *          3. Select() for connection timeout management
 *          4. Critical bug fix: Socket connection status verification
 *          5. 100% system call based, no external tool dependencies
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
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

// ====================== Configuration Constants ======================
/** Target network interface name */
#define NET_DEV_NAME     "eth0"
/** Public DNS server (Alibaba) for connectivity test */
#define NET_DNS_SERVER   "223.5.5.5"
/** DNS standard port */
#define NET_DNS_PORT     53
/** Connection timeout value (seconds) */
#define NET_CHECK_TIMEOUT 3

// ====================== Private Helper Functions ======================
/**
 * @brief   Set socket file descriptor to non-blocking mode
 * @param   fd  Socket file descriptor
 * @return  0 on success, -1 on failure
 * @details Used for asynchronous connection with timeout
 */
static int sock_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ====================== Public API Implementations ======================
/**
 * @brief   Get eth0 physical link status
 */
bool NetCheck_GetEth0LinkStatus(void)
{
    // Create UDP socket for ioctl operation
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) return false;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, NET_DEV_NAME, IFNAMSIZ - 1);

    // Get network interface flags via ioctl
    int ret = ioctl(sock_fd, SIOCGIFFLAGS, &ifr);
    close(sock_fd);

    if (ret < 0) return false;
    // Check interface UP state and physical link (RUNNING)
    return ((ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING));
}

/**
 * @brief   Check internet connectivity with non-blocking socket
 */
bool NetCheck_GetInternetStatus(void)
{
    // Create TCP socket for connection test
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) return false;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_port        = htons(NET_DNS_PORT);
    inet_pton(AF_INET, NET_DNS_SERVER, &serv_addr.sin_addr);

    // Set non-blocking mode for timeout control
    sock_set_nonblock(sock_fd);
    int conn_ret = connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    bool net_ok = false;
    if (conn_ret == 0) {
        // Connection succeeded immediately
        net_ok = true;
    } else if (errno == EINPROGRESS) {
        // Connection in progress, use select to wait for result
        fd_set wfds;
        struct timeval tv;
        FD_ZERO(&wfds);
        FD_SET(sock_fd, &wfds);

        tv.tv_sec  = NET_CHECK_TIMEOUT;
        tv.tv_usec = 0;

        int sel_ret = select(sock_fd + 1, NULL, &wfds, NULL, &tv);
        if (sel_ret > 0) {
            // Critical BUG FIX: Verify actual socket connection status
            // Root cause: select writable does not guarantee successful connection
            int error = 0;
            socklen_t len = sizeof(error);
            getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &len);
            if (error == 0) net_ok = true;
        }
    }

    // Cleanup socket resource
    close(sock_fd);
    return net_ok;
}