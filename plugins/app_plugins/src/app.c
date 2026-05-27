/* SPDX-License-Identifier: MIT */
/**
 * @file app.c
 * @brief 产品版应用层（纯事件总线解耦，无直接API调用）
 * @author Luo
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

#define MODULE_NAME        "APP"
#define MODULE_TAG         "[APP]"
#define APP_EVENT_BUS      SYS_EVENT_BUS_NAME
#define WAIT_MODULE_READY  300000  // 300ms 等待底层初始化

static volatile bool g_app_running = true;

// 仅处理系统退出事件
static void _app_sys_cb(const event_t *event, void *data) {
    (void)data;
    if (event->type == EVENT_TYPE_SYS_SHUTDOWN || 
        event->type == EVENT_TYPE_SYS_STOP ||
        event->type == EVENT_TYPE_SYS_ERROR) {
        LOG_I(MODULE_TAG " 收到退出信号，执行安全关闭");
        g_app_running = false;
    }
}

// 纯事件总线启动（完美解耦）
static void _app_auto_start(void) {
    LOG_I(MODULE_TAG " 等待底层模块初始化完成...");
    usleep(WAIT_MODULE_READY);

    LOG_I(MODULE_TAG " 发送系统启动指令 RESUME");
    // 发布启动事件，所有服务自动响应
    event_bus_publish_simple(APP_EVENT_BUS, EVENT_TYPE_SYS_RESUME, MODULE_NAME);
    // 立即分发事件（确保服务立刻收到）
    event_bus_dispatch(APP_EVENT_BUS);

    LOG_I(MODULE_TAG " 启动指令已成功分发至所有服务");
}

// 产品主循环（保活+事件分发）
static void app_run(void) {
    int bus_fd = event_bus_get_wait_fd(APP_EVENT_BUS);
    if (bus_fd < 0) {
        LOG_E(MODULE_TAG " 获取事件总线FD失败");
        return;
    }

    LOG_I(MODULE_TAG " 产品模式运行中，无人值守自动工作");
    while (g_app_running) {
        fd_set fds;
        struct timeval tv = {0, 30000};
        FD_ZERO(&fds);
        FD_SET(bus_fd, &fds);

        int ret = select(bus_fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(bus_fd, &fds)) {
            event_bus_dispatch(APP_EVENT_BUS);
        }
    }
}

// 初始化订阅
static int app_init(void) {
    event_subscriber_t sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _app_sys_cb,
        .skip_self_published = false
    };
    event_bus_subscribe_ex(APP_EVENT_BUS, &sub, MODULE_NAME);
    LOG_I(MODULE_TAG " 产品APP初始化完成");
    return 0;
}

// 线程入口
static void *app_thread(void *arg) {
    (void)arg;
    app_init();
    _app_auto_start();
    app_run();
    return NULL;
}

// 自动加载
static int __app_auto_init(void) {
    pthread_t tid;
    pthread_attr_t thread_attr;
    struct sched_param sched_param;
    /* 初始化线程属性 + 设置实时优先级 */
    pthread_attr_init(&thread_attr);
    pthread_attr_setschedpolicy(&thread_attr, SCHED_FIFO);
    sched_param.sched_priority = 60;
    pthread_attr_setschedparam(&thread_attr, &sched_param);
    pthread_attr_setinheritsched(&thread_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_create(&tid, &thread_attr, app_thread, NULL);
    pthread_detach(tid);
    pthread_attr_destroy(&thread_attr);
    return 0;
}
// 仅产品模式编译
#if RUN_PRODUCT_MODE
    MODULE_INIT_LEVEL(INIT_APP, __app_auto_init);
#endif