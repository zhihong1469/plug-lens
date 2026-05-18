/* SPDX-License-Identifier: MIT */
/**
 * @file demo_app.c
 * @brief 应用层核心Demo（纯事件总线控制）
 * @details 修复：键盘防抖、精准订阅、无重复触发
 * @author Luo
 * @date 2026-05-31
 */
#include "log.h"
#include "vision_ai_config.h"
#include "event_bus.h"
#include "initcall.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>

// ==========================================================================
// 模块配置
// ==========================================================================
#define MODULE_NAME               "DEMO_APP"
#define MODULE_TAG                "[DEMO_APP]"
#define SYS_EVENT_BUS_NAME        "sys_event"
#define APP_LOOP_WAIT_US          30000   // 30ms 主循环等待
#define KEY_DEBOUNCE_US           50000   // 50ms 键盘防抖（关键修复）

// ==========================================================================
// 应用上下文
// ==========================================================================
typedef struct {
    // 8字节 对齐占位（保证整体结构最优）
    int                     sub_sys;        // 系统订阅ID  4B
    int                     sub_capture;    // 采集订阅ID  4B
    int                     sub_face;       // 人脸订阅ID  4B
    // 1字节 volatile 状态（紧凑排列）
    volatile bool           app_running;    // 运行状态    1B
    volatile bool           is_paused;      // 暂停状态    1B
    volatile bool           key_processing; // 键盘防抖    1B
    
    // 1字节 服务状态
    bool                    cap_ready;      // 采集就绪    1B
    bool                    face_ready;     // 人脸就绪    1B
} demo_app_t;

static demo_app_t s_demo;

// ==========================================================================
// 内部函数
// ==========================================================================
static void _demo_print_help(void);
static void _demo_handle_key(char cmd);

// ==========================================================================
// 系统事件回调（精准订阅，无冗余）
// ==========================================================================
static void _demo_sys_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    demo_app_t *srv = &s_demo;

    switch (event->type) {
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG " 系统核心初始化完成");
            break;
        case EVENT_TYPE_SYS_PAUSE:
            if (!srv->is_paused) {
                LOG_I(MODULE_TAG " 系统已暂停");
                srv->is_paused = true;
            }
            break;
        case EVENT_TYPE_SYS_RESUME:
            if (srv->is_paused) {
                LOG_I(MODULE_TAG " 系统已恢复");
                srv->is_paused = false;
            }
            break;
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            LOG_I(MODULE_TAG " 收到退出事件，安全关闭");
            srv->app_running = false;
            break;
        case EVENT_TYPE_SYS_ERROR:
            LOG_E(MODULE_TAG " 系统异常，强制退出");
            srv->app_running = false;
            break;
        default:
            break;
    }
}

// ==========================================================================
// 采集服务回调（仅订阅单个事件，修复订阅错误）
// ==========================================================================
static void _demo_cap_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    demo_app_t *srv = &s_demo;

    if (event->type == EVENT_TYPE_CAPTURE_READY && !srv->cap_ready) {
        LOG_I(MODULE_TAG " 采集服务就绪");
        srv->cap_ready = true;
    }
    if (event->type == EVENT_TYPE_CAPTURE_STOPPED && srv->cap_ready) {
        LOG_I(MODULE_TAG " 采集服务停止");
        srv->cap_ready = false;
    }
}

// ==========================================================================
// 人脸服务回调（仅订阅单个事件，修复订阅错误）
// ==========================================================================
static void _demo_face_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    demo_app_t *srv = &s_demo;

    if (event->type == EVENT_TYPE_FACE_READY && !srv->face_ready) {
        LOG_I(MODULE_TAG " 人脸服务就绪");
        srv->face_ready = true;
    }
    if (event->type == EVENT_TYPE_FACE_STOPPED && srv->face_ready) {
        LOG_I(MODULE_TAG " 人脸服务停止");
        srv->face_ready = false;
    }
}

// ==========================================================================
// 帮助菜单
// ==========================================================================
static void _demo_print_help(void)
{
    LOG_I(MODULE_TAG " ========================================");
    LOG_I(MODULE_TAG "  键盘控制：");
    LOG_I(MODULE_TAG "    s - 恢复系统");
    LOG_I(MODULE_TAG "    t - 暂停系统");
    LOG_I(MODULE_TAG "    q - 退出应用");
    LOG_I(MODULE_TAG "    h - 帮助");
    LOG_I(MODULE_TAG " ========================================");
}

// ==========================================================================
// 键盘命令处理（核心：防抖+无重复）
// ==========================================================================
static void _demo_handle_key(char cmd)
{
    demo_app_t *srv = &s_demo;
    if (srv->key_processing) return;  // 防抖：正在处理则直接返回

    srv->key_processing = true;
    LOG_I(MODULE_TAG " 执行命令: %c", cmd);

    switch (cmd) {
        case 's': case 'S':
            event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_SYS_RESUME, MODULE_NAME);
            break;
        case 't': case 'T':
            event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_SYS_PAUSE, MODULE_NAME);
            break;
        case 'q': case 'Q':
            event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_SYS_SHUTDOWN, MODULE_NAME);
            break;
        case 'h': case 'H':
            _demo_print_help();
            break;
        default:
            LOG_W(MODULE_TAG " 未知命令");
            break;
    }

    usleep(KEY_DEBOUNCE_US);  // 强制防抖延迟
    srv->key_processing = false;
}

// ==========================================================================
// 初始化（精准订阅 + 扩展订阅：跳过自己发布的事件）
// ==========================================================================
static int demo_app_init(void)
{
    demo_app_t *srv = &s_demo;
    memset(srv, 0, sizeof(demo_app_t));

    srv->app_running = true;
    srv->is_paused = false;
    srv->key_processing = false;

    // ===================== 核心修复：使用扩展订阅，跳过自己发布的事件 =====================
    // 1. 订阅系统事件
    event_subscriber_t sys_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _demo_sys_event_cb,
        .skip_self_published = true  // 关键：自己发的事件自己不收
    };
    srv->sub_sys = event_bus_subscribe_ex(SYS_EVENT_BUS_NAME, &sys_sub, MODULE_NAME);

    // 2. 精准订阅采集服务事件（修复区间订阅错误）
    event_subscriber_t cap_sub = {
        .event_type = EVENT_TYPE_INVALID,  // 只监听需要的采集事件
        .callback = _demo_cap_event_cb,
        .skip_self_published = true
    };
    srv->sub_capture = event_bus_subscribe_ex(SYS_EVENT_BUS_NAME, &cap_sub, MODULE_NAME);

    // 3. 精准订阅人脸服务事件
    event_subscriber_t face_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _demo_face_event_cb,
        .skip_self_published = true
    };
    srv->sub_face = event_bus_subscribe_ex(SYS_EVENT_BUS_NAME, &face_sub, MODULE_NAME);

    if (srv->sub_sys <0 || srv->sub_capture <0 || srv->sub_face <0) {
        LOG_E(MODULE_TAG " 订阅失败");
        return -1;
    }

    _demo_print_help();
    LOG_I(MODULE_TAG " 初始化完成");
    return 0;
}

// ==========================================================================
// 主循环（修复键盘重复触发）
// ==========================================================================
static void demo_app_run(void)
{
    demo_app_t *srv = &s_demo;
    int bus_fd = event_bus_get_wait_fd(SYS_EVENT_BUS_NAME);

    if (bus_fd < 0) {
        LOG_E(MODULE_TAG " 获取总线FD失败");
        return;
    }

    int max_fd = (bus_fd > STDIN_FILENO) ? bus_fd : STDIN_FILENO;
    LOG_I(MODULE_TAG " 运行中，按 q 退出");

    while (srv->app_running) {
        fd_set read_fds;
        struct timeval tv = {0, APP_LOOP_WAIT_US};

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(bus_fd, &read_fds);

        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ret < 0 && errno == EINTR) continue;
        if (ret < 0) break;

        // 处理事件总线
        if (FD_ISSET(bus_fd, &read_fds)) {
            event_bus_dispatch(SYS_EVENT_BUS_NAME);
        }

        // 处理键盘（极简无残留）
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char cmd = 0;
            ssize_t n = read(STDIN_FILENO, &cmd, 1);
            if (n == 1 && cmd != '\n' && cmd != '\r') {
                _demo_handle_key(cmd);
            }
        }
    }

    LOG_I(MODULE_TAG " 主循环退出");
}

// ==========================================================================
// 反初始化
// ==========================================================================
static void demo_app_deinit(void)
{
    demo_app_t *srv = &s_demo;
    event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->sub_face);
    event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->sub_capture);
    event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->sub_sys);
    LOG_I(MODULE_TAG " 资源清理完成");
}

// ==========================================================================
// 线程入口
// ==========================================================================
static void *_demo_thread(void *arg)
{
    (void)arg;
    demo_app_init();
    demo_app_run();
    demo_app_deinit();
    return NULL;
}

// ==========================================================================
// 自动初始化
// ==========================================================================
static int __demo_auto_init(void)
{
    pthread_t tid;
    pthread_create(&tid, NULL, _demo_thread, NULL);
    pthread_detach(tid);
    LOG_I(MODULE_TAG " 加载完成");
    return 0;
}
MODULE_INIT_LEVEL(INIT_APP, __demo_auto_init);