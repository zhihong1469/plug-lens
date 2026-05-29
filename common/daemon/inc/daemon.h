/* SPDX-License-Identifier: MIT */
/**
 * @file    daemon.h
 * @brief   Standard Linux daemon process management interface
 * @details Core module capabilities:
 *          1. Implements embedded Linux standard double-fork daemon creation logic
 *          2. Detaches from terminal control, runs silently in background for production deployment
 *          3. Adapts to plug-lens Vision AI terminal background service hosting
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global usage rules:
 *          1. Must be called at the earliest stage of program initialization, before all business logic
 *          2. Only supported in product mode (RUN_PRODUCT_MODE=1)
 *          3. Standard input/output/error are redirected to /dev/null after calling
 */
#ifndef DAEMON_H
#define DAEMON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Create standard Linux daemon process with double-fork mode
 * @return  0 on success, -1 on failure (system call error)
 *
 * @pre     Preconditions:
 *          1. Program is in initialization phase, no business threads/services started
 *          2. Product mode (RUN_PRODUCT_MODE=1) enabled, forbidden in debug mode
 *          3. Process has sufficient system permissions to create child processes
 *
 * @post    Postconditions:
 *          1. Parent process exits, child process becomes a child of init process (PID 1)
 *          2. Detached from controlling terminal, no standard I/O available
 *          3. Working directory switched to root directory
 *          4. File mode mask reset, unused file descriptors closed
 *
 * @note    Usage notes:
 *          1. Adopts industrial-grade double-fork mechanism to avoid acquiring controlling terminal
 *          2. Automatically redirects stdin/stdout/stderr to /dev/null
 *          3. Daemon lifecycle synchronized with system, supports auto-start on boot
 *
 * @warning Warnings:
 *          - Forbidden to call in debug mode, thread context, or signal handler
 *          - Forbidden to use terminal I/O functions (printf/scanf) after calling
 *          - Exit program immediately on failure, do not continue business logic
 *
 * @thread_safety No
 *                Singleton initialization API, only allowed to be called once by main thread at startup
 *
 * @example Usage example:
 * @code
 * // Initialize daemon in product mode
 * #if RUN_PRODUCT_MODE
 * if (create_daemon() != 0) {
 *     // Daemon creation failed, exit program
 *     return -1;
 * }
 * #endif
 *
 * // Start business logic later (vision capture, AI inference, streaming service)
 * @endcode
 */
int create_daemon(void);

#ifdef __cplusplus
}
#endif

#endif // DAEMON_H