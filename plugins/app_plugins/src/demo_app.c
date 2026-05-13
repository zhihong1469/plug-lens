/* SPDX-License-Identifier: MIT */
/**
 * @file demo_app.c
 * @brief 【Linux内核式实现】采集+人脸检测 自动运行Demo
 * @details 1. 基于initcall自动初始化/启动
 *          2. 100% 事件总线驱动，无文件描述符监听
 *          3. 全模块解耦，自治运行 + 优雅退出
 *          4. 仅通过数据/事件总线与系统交互，完全自洽
 */

#include "main.h"
#include "capture_srv.h"
#include "face_detect_srv.h"
#include "log.h"
#include "initcall.h"
#include "ai_model_base.h"
#include "vision_ai_config.h"
#include "data_bus.h"
#include "event_bus.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

// AI人脸信息定义
#include "ai_model_base.h"

#define MODULE_NAME "DEMO_APP"

// ====================== 框架统一总线名称 ======================
#define SYS_DATA_BUS_NAME     "sys_data"
#define SYS_EVENT_BUS_NAME    "sys_event"

// ====================== 应用层私有全局变量 ======================
static pthread_t g_demo_tid;
static data_bus_subscription_handle_t g_ai_sub = NULL;   // AI结果订阅
static int g_sys_sub_id = -1;                           // 系统事件订阅
static volatile bool g_app_running = false;              // 应用运行标志（事件驱动）

// ====================== 1. 数据总线：AI结果打印回调 ======================
static void _ai_result_cb(data_bus_item_handle_t item, void *user_data)
{
    // (void)user_data;
    // if (!item || !g_app_running) {
    //     data_bus_release(item);
    //     return;
    // }

    // if (data_bus_get_item_type(item) != DATA_TYPE_AI_RESULT) {
    //     data_bus_release(item);
    //     return;
    // }

    // const FaceInfo_C *faces = (const FaceInfo_C *)data_bus_get_readonly_ptr(item);
    // size_t data_size = data_bus_get_item_size(item);
    // int face_num = data_size / sizeof(FaceInfo_C);

    // // 1秒打印防抖
    // static uint64_t last_ts = 0;
    // struct timespec ts;
    // clock_gettime(CLOCK_MONOTONIC, &ts);
    // uint64_t now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    // if (now - last_ts < 1000) {
    //     data_bus_release(item);
    //     return;
    // }
    // last_ts = now;

    // LOG_I("[DEMO] 检测到人脸数: %d", face_num);
    // for (int i = 0; i < face_num; i++) {
    //     LOG_I("  人脸%d: [%.0f,%.0f]-[%.0f,%.0f] 置信度:%.2f",
    //         i+1, faces[i].x1, faces[i].y1, faces[i].x2, faces[i].y2, faces[i].score);
    // }

    // data_bus_release(item);
}

// ====================== 2. 事件总线：系统级事件回调（核心退出/控制） ======================
static void _sys_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    if (!event) return;

    switch (event->type) {
        // 系统停止/关机：应用层退出
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            LOG_I("%s: 收到系统停止事件，准备退出", MODULE_NAME);
            g_app_running = false;
            break;

        // 系统暂停/恢复：可扩展业务逻辑
        case EVENT_TYPE_SYS_PAUSE:
            LOG_I("%s: 收到系统暂停事件", MODULE_NAME);
            break;
        case EVENT_TYPE_SYS_RESUME:
            LOG_I("%s: 收到系统恢复事件", MODULE_NAME);
            break;

        default:
            break;
    }
}

// ====================== 应用层初始化（仅订阅总线） ======================
static int _demo_init(void)
{
    // 订阅系统全局事件（退出/控制全靠事件总线）
    event_subscriber_t sys_sub = {
        .event_type = EVENT_TYPE_INVALID,  // 订阅所有系统事件
        .callback = _sys_event_cb,
        .user_data = NULL
    };
    g_sys_sub_id = event_bus_subscribe(SYS_EVENT_BUS_NAME, &sys_sub);
    if (g_sys_sub_id < 0) {
        LOG_E("%s: 订阅系统事件失败", MODULE_NAME);
        return -1;
    }

    // 订阅AI结果数据总线
    if (data_bus_subscribe(SYS_DATA_BUS_NAME,
                           DATA_TYPE_AI_RESULT,
                           _ai_result_cb,
                           NULL,
                           &g_ai_sub))
    {
        LOG_E("%s: 订阅AI结果失败", MODULE_NAME);
        event_bus_unsubscribe(SYS_EVENT_BUS_NAME, g_sys_sub_id);
        g_sys_sub_id = -1;
        return -2;
    }

    g_app_running = true;
    LOG_I("%s: 初始化完成（事件总线驱动）", MODULE_NAME);
    return 0;
}

// ====================== 应用层清理（仅释放自身订阅） ======================
static void _demo_cleanup(void)
{
    // 取消数据总线订阅
    if (g_ai_sub) {
        data_bus_unsubscribe(SYS_DATA_BUS_NAME, &g_ai_sub);
        g_ai_sub = NULL;
    }
    // 取消系统事件订阅
    if (g_sys_sub_id >= 0) {
        event_bus_unsubscribe(SYS_EVENT_BUS_NAME, g_sys_sub_id);
        g_sys_sub_id = -1;
    }

    LOG_I("%s: 资源清理完成，完全退出", MODULE_NAME);
}

// ====================== 应用层工作线程（纯事件驱动，零CPU占用） ======================
static void *_demo_thread(void *arg)
{
    (void)arg;

    // 1. 初始化（仅订阅总线）
    if (_demo_init() != 0) {
        LOG_E("%s: 初始化失败，线程退出", MODULE_NAME);
        return NULL;
    }

    LOG_I("%s: ========================================", MODULE_NAME);
    LOG_I("%s:  应用层运行中（事件总线自治）", MODULE_NAME);
    LOG_I("%s: ========================================", MODULE_NAME);

    // 2. 事件驱动主循环：仅休眠，等待事件总线修改运行标志
    while (g_app_running) {
        usleep(20000);  // 20ms休眠，极低CPU占用
    }

    // 3. 优雅退出清理
    _demo_cleanup();
    LOG_I("%s: 线程正常退出", MODULE_NAME);
    return NULL;
}

// ====================== 内核式自动初始化 ======================
static int __demo_auto_init(void)
{
    LOG_I("========================================", MODULE_NAME);
    LOG_I("  【事件总线自治】Demo应用 自动加载", MODULE_NAME);
    LOG_I("  全模块解耦 | 无FD | 纯总线交互", MODULE_NAME);
    LOG_I("========================================", MODULE_NAME);

    // 创建独立线程
    int ret = pthread_create(&g_demo_tid, NULL, _demo_thread, NULL);
    if (ret != 0) {
        LOG_E("%s: 创建线程失败", MODULE_NAME);
        return -1;
    }
    pthread_detach(g_demo_tid);
    return 0;
}

// 注册到系统初始化段
MODULE_INIT(__demo_auto_init);