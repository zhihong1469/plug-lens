/* SPDX-License-Identifier: MIT */
/**
 * @file demo_app.c
 * @brief 【Linux内核式实现】采集+人脸检测 自动运行Demo
 * @details 1. 基于initcall自动初始化/启动
 *          2. 独立线程运行业务，不侵入main主循环
 *          3. 优雅退出，完全对标Linux内核设计
 */

#include "main.h"
#include "capture_srv.h"
#include "face_detect_srv.h"
#include "log.h"
#include "service_base.h"
#include "initcall.h"
// 引入全局配置头文件
#include "vision_ai_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include <pthread.h>
// AI模块头文件
#include "ai_model_mnn.hpp"

#define MODULE_NAME "DEMO_APP"

// ====================== 全局句柄（线程安全） ======================
static app_context_t *g_ctx = NULL;
static service_base_t *g_cap_srv = NULL;
static face_detect_srv_handle_t *g_face_srv = NULL;
static data_bus_subscription_handle_t g_ai_sub = NULL;
static pthread_t g_demo_tid;

// ====================== AI结果打印回调 ======================
static void _ai_result_cb(data_bus_item_handle_t item, void *user_data)
{
    (void)user_data;
    if (!item) return;

    data_bus_item_info_t info;
    if (data_bus_get_item_info(item, &info) != 0) goto exit;
    if (info.type != DATA_TYPE_AI_RESULT) goto exit;

    const FaceInfo_C *faces = (const FaceInfo_C *)data_bus_get_readonly_ptr(item);
    int face_num = info.data_size / sizeof(FaceInfo_C);

    // 1秒打印一次，避免刷屏
    static uint64_t last_ts = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = ts.tv_sec*1000 + ts.tv_nsec/1000000;
    if (now - last_ts < 1000) goto exit;
    last_ts = now;

    LOG_I("[DEMO] 检测到人脸数: %d", face_num);
    for (int i = 0; i < face_num; i++) {
        LOG_I("  人脸%d: [%.0f,%.0f]-[%.0f,%.0f] 置信度:%.2f",
            i+1, faces[i].x1, faces[i].y1, faces[i].x2, faces[i].y2, faces[i].score);
    }

exit:
    data_bus_release(item);
}

// ====================== 服务初始化 ======================
static int _demo_init(void)
{
    g_ctx = app_get_context();
    if (!g_ctx || !g_ctx->evt_bus || !g_ctx->data_bus) return -1;

    // 初始化采集服务
    g_cap_srv = capture_srv_get_instance();
    if (!g_cap_srv || service_base_init(g_cap_srv)) return -2;

    // 初始化人脸检测服务 → 【全部使用全局宏配置，无硬编码】
    face_detect_cfg_t cfg = {
        .evt_bus = g_ctx->evt_bus,
        .data_bus = g_ctx->data_bus,
        .ai_cfg = {
            .model_path = CONFIG_AI_MODEL_PATH,
            .input_width = CONFIG_AI_INPUT_W,
            .input_height = CONFIG_AI_INPUT_H,
            .score_thresh = CONFIG_AI_SCORE_THRESH,
            .iou_thresh = CONFIG_AI_IOU_THRESH,
        }
    };
    g_face_srv = face_detect_srv_create(&cfg);
    if (!g_face_srv) return -3;

    LOG_I("%s: 服务初始化完成", MODULE_NAME);
    return 0;
}

// ====================== 服务启动 ======================
static int _demo_start(void)
{
    // 启动采集
    if (service_base_start(g_cap_srv)) return -1;
    // 启动人脸检测
    if (face_detect_srv_start(g_face_srv)) return -2;
    // 订阅AI结果
    if (data_bus_subscribe(g_ctx->data_bus, DATA_TYPE_AI_RESULT, _ai_result_cb, NULL, &g_ai_sub)) return -3;

    LOG_I("%s: 服务启动完成，运行中...", MODULE_NAME);
    return 0;
}

// ====================== 资源清理 ======================
static void _demo_cleanup(void)
{
    if (g_ai_sub) data_bus_unsubscribe(g_ctx->data_bus, &g_ai_sub);
    if (g_face_srv) { face_detect_srv_stop(g_face_srv); face_detect_srv_destroy(&g_face_srv); }
    if (g_cap_srv) { service_base_stop(g_cap_srv); service_base_deinit(g_cap_srv); }
    LOG_I("%s: 资源清理完成", MODULE_NAME);
}

// ====================== Demo 独立线程主循环 ======================
static void *_demo_thread(void *arg)
{
    (void)arg;
    fd_set fds;
    int ret;

    // 1. 初始化
    if (_demo_init() != 0) {
        LOG_E("%s: 初始化失败，线程退出", MODULE_NAME);
        return NULL;
    }

    // 2. 启动
    if (_demo_start() != 0) {
        LOG_E("%s: 启动失败，线程退出", MODULE_NAME);
        _demo_cleanup();
        return NULL;
    }

    // 3. 业务主循环（零CPU占用）
    while (g_ctx->app_running) {
        FD_ZERO(&fds);
        int exit_fd = g_ctx->exit_pipe[0];
        int evt_fd = event_bus_get_wait_fd(g_ctx->evt_bus);

        FD_SET(exit_fd, &fds);
        if (evt_fd > 0) FD_SET(evt_fd, &fds);

        ret = select((exit_fd > evt_fd ? exit_fd : evt_fd) + 1, &fds, NULL, NULL, NULL);
        if (ret < 0 && errno != EINTR) break;

        // 退出信号
        if (FD_ISSET(exit_fd, &fds)) break;
        // 分发事件
        if (evt_fd > 0 && FD_ISSET(evt_fd, &fds)) event_bus_dispatch(g_ctx->evt_bus);
    }

    // 4. 退出清理
    _demo_cleanup();
    return NULL;
}

// ====================== 【内核核心】自动初始化函数 ======================
static int __demo_auto_init(void)
{
    LOG_I("========================================");
    LOG_I("  【内核式自动启动】采集+人脸检测 Demo");
    LOG_I("========================================");

    // 创建独立线程，不阻塞main主循环
    int ret = pthread_create(&g_demo_tid, NULL, _demo_thread, NULL);
    if (ret) {
        LOG_E("%s: 创建线程失败", MODULE_NAME);
        return -1;
    }
    // 线程分离，自动释放资源
    pthread_detach(g_demo_tid);
    return 0;
}

// ====================== 【内核核心】注册到initcall段 ======================
MODULE_INIT(__demo_auto_init);