/* SPDX-License-Identifier: MIT */
#include "face_detect_srv.h"
#include "log.h"
#include "queue.h"
#include "data_bus.h"
#include "event_bus.h"
#include "vision_ai_config.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// ==========================================================================
// 【框架对齐】系统总线固定名称 + 宏定义
// ==========================================================================
#define SYS_DATA_BUS_NAME     "sys_data"
#define SYS_EVENT_BUS_NAME    "sys_event"
#define MODULE_NAME           "FACE_DETECT"
#define MAX_FACES             10
#define FACE_SRV_NAME         "face_detect_srv"

// ==========================================================================
// 人脸检测服务 私有结构体（首成员=服务基类，纯C继承）
// ==========================================================================
typedef struct {
    // 基类（必须放在第一个）
    service_base_t        base;

    // AI模型
    ai_model_handle_t    *ai_model;
    FaceInfo_C            faces[MAX_FACES];
    int                   face_num;

    // 服务状态
    srv_state_t      state;
    pthread_mutex_t       state_mutex;
    bool                  thread_running;
    pthread_t             work_thread;

    // 订阅句柄/ID
    int                   evt_sub_id;
    data_bus_subscription_handle_t data_sub;
} face_detect_srv_t;

// 静态单例（对外完全隐藏）
static face_detect_srv_t s_face_srv;

// ==========================================================================
// 前置声明（对齐采集服务）
// ==========================================================================
static int  face_detect_init(void *self);
static int  face_detect_start(void *self);
static int  face_detect_pause(void *self);
static int  face_detect_resume(void *self);
static int  face_detect_stop(void *self);
static int  face_detect_deinit(void *self);
static void face_detect_event_handle(void *self, uint32_t event_id, void *data);
static void *face_detect_work_thread(void *arg);
static void _data_bus_cb(data_bus_item_handle_t item, void *user_data);
static void _event_bus_cb(const event_t *event, void *user_data);

// ==========================================================================
// 服务操作表（7个生命周期接口，框架自动调用）
// ==========================================================================
static const service_ops_t s_face_detect_ops = {
    .init        = face_detect_init,
    .start       = face_detect_start,
    .pause       = face_detect_pause,
    .resume      = face_detect_resume,
    .stop        = face_detect_stop,
    .deinit      = face_detect_deinit,
    .event_handle = face_detect_event_handle
};

// ==========================================================================
// 对外接口：获取服务实例
// ==========================================================================
service_base_t *face_detect_srv_get_instance(void)
{
    return &s_face_srv.base;
}

// ==========================================================================
// 工具函数：状态锁
// ==========================================================================
static void _lock_state(face_detect_srv_t *srv) {
    pthread_mutex_lock(&srv->state_mutex);
}

static void _unlock_state(face_detect_srv_t *srv) {
    pthread_mutex_unlock(&srv->state_mutex);
}

// ==========================================================================
// 数据总线回调：视频帧 → AI推理 → 发布结果（业务逻辑完全保留）
// ==========================================================================
static void _data_bus_cb(data_bus_item_handle_t item, void *user_data)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)user_data;
    if (!srv || srv->state != EVENT_TYPE_FACE_RUNNING) {
        data_bus_release(item);
        return;
    }

    // 1. 获取只读数据
    const void *data = data_bus_get_readonly_ptr(item);

    // 2. 仅处理RGB帧
    if (data_bus_get_item_type(item) != DATA_TYPE_VIDEO_FRAME_RGB) {
        data_bus_release(item);
        return;
    }

    // 3. AI人脸检测推理
    srv->face_num = 0;
    int ret = ai_model_mnn_infer_yuyv((const uint8_t *)data,
                                     CONFIG_CAPTURE_WIDTH, CONFIG_CAPTURE_HEIGHT,
                                     srv->faces, MAX_FACES, &srv->face_num);
    if (ret != 0 || srv->face_num <= 0) {
        data_bus_release(item);
        return;
    }

    LOG_D("%s: Detect %d faces", MODULE_NAME, srv->face_num);

    // 4. 【新总线】发布AI结果
    data_bus_item_handle_t ai_item = NULL;
    if (data_bus_alloc(SYS_DATA_BUS_NAME,
                       DATA_TYPE_AI_RESULT,
                       sizeof(FaceInfo_C) * srv->face_num,
                       MODULE_NAME, &ai_item) == 0)
    {
        void *w_ptr = data_bus_get_writable_ptr(ai_item);
        memcpy(w_ptr, srv->faces, sizeof(FaceInfo_C) * srv->face_num);
        data_bus_publish(SYS_DATA_BUS_NAME, ai_item);
        data_bus_release(ai_item);
    }

    data_bus_release(item);
}

// ==========================================================================
// 事件总线回调（系统控制指令）
// ==========================================================================
static void _event_bus_cb(const event_t *event, void *user_data)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)user_data;
    if (!srv || !event) return;
    face_detect_event_handle(srv, event->type, event->data);
}

// ==========================================================================
// 工作线程（事件驱动，空循环）
// ==========================================================================
static void *face_detect_work_thread(void *arg)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)arg;
    LOG_I("%s: work thread start", MODULE_NAME);

    while (srv->thread_running) {
        sleep(1);
    }

    LOG_I("%s: work thread exit", MODULE_NAME);
    return NULL;
}

// ==========================================================================
// 1. 初始化接口（IDLE → INIT）
// ==========================================================================
static int face_detect_init(void *self)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)self;
    if (!srv) return -1;

    // 基类初始化
    srv->base.state = SRV_STATE_INIT;
    srv->base.ops   = &s_face_detect_ops;
    srv->base.name  = FACE_SRV_NAME;

    // 成员初始化
    srv->state = EVENT_TYPE_FACE_STOPPED;
    srv->thread_running = false;
    srv->evt_sub_id = -1;
    srv->face_num = 0;
    pthread_mutex_init(&srv->state_mutex, NULL);

    // AI模型初始化
    ai_model_config_t ai_cfg = {0};
    srv->ai_model = ai_model_mnn_create(&ai_cfg);
    if (!srv->ai_model) {
        LOG_E("%s: AI model create failed", MODULE_NAME);
        return -2;
    }

    LOG_I("%s: init success", MODULE_NAME);
    return 0;
}

// ==========================================================================
// 2. 启动接口（INIT → RUNNING）
// ==========================================================================
static int face_detect_start(void *self)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)self;
    if (!srv || srv->base.state != SRV_STATE_INIT) return -1;

    _lock_state(srv);

    // 【新总线】订阅系统事件
    event_subscriber_t sub = {0};
    sub.event_type = EVENT_TYPE_INVALID;
    sub.callback = _event_bus_cb;
    sub.user_data = srv;
    srv->evt_sub_id = event_bus_subscribe(SYS_EVENT_BUS_NAME, &sub);

    // 【新总线】订阅视频数据
    data_bus_subscribe(SYS_DATA_BUS_NAME,
                       DATA_TYPE_VIDEO_FRAME_RGB,
                       _data_bus_cb,
                       srv,
                       &srv->data_sub);

    // 启动工作线程
    srv->thread_running = true;
    pthread_create(&srv->work_thread, NULL, face_detect_work_thread, srv);

    // 状态切换
    srv->state = EVENT_TYPE_FACE_RUNNING;
    srv->base.state = SRV_STATE_RUNNING;
    _unlock_state(srv);

    // 发布事件
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_READY, MODULE_NAME);
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_RUNNING, MODULE_NAME);
    LOG_I("%s: started", MODULE_NAME);

    return 0;
}

// ==========================================================================
// 3. 暂停接口
// ==========================================================================
static int face_detect_pause(void *self)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)self;
    if (!srv || srv->base.state != SRV_STATE_RUNNING) return -1;

    _lock_state(srv);
    srv->state = EVENT_TYPE_FACE_STOPPED;
    srv->base.state = SRV_STATE_PAUSE;
    _unlock_state(srv);

    LOG_I("%s: paused", MODULE_NAME);
    return 0;
}

// ==========================================================================
// 4. 恢复接口
// ==========================================================================
static int face_detect_resume(void *self)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)self;
    if (!srv || srv->base.state != SRV_STATE_PAUSE) return -1;

    _lock_state(srv);
    srv->state = EVENT_TYPE_FACE_RUNNING;
    srv->base.state = SRV_STATE_RUNNING;
    _unlock_state(srv);

    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_RUNNING, MODULE_NAME);
    LOG_I("%s: resumed", MODULE_NAME);
    return 0;
}

// ==========================================================================
// 5. 停止接口
// ==========================================================================
static int face_detect_stop(void *self)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)self;
    if (!srv || srv->base.state == SRV_STATE_STOP) return 0;

    _lock_state(srv);
    srv->thread_running = false;
    srv->state = EVENT_TYPE_FACE_STOPPED;
    srv->base.state = SRV_STATE_STOP;
    _unlock_state(srv);

    // 等待线程退出
    pthread_join(srv->work_thread, NULL);

    // 【新总线】取消订阅
    if (srv->evt_sub_id >= 0) {
        event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->evt_sub_id);
        srv->evt_sub_id = -1;
    }
    data_bus_unsubscribe(SYS_DATA_BUS_NAME, &srv->data_sub);

    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_STOPPED, MODULE_NAME);
    LOG_I("%s: stopped", MODULE_NAME);
    return 0;
}

// ==========================================================================
// 6. 销毁接口
// ==========================================================================
static int face_detect_deinit(void *self)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)self;
    if (!srv) return -1;

    face_detect_stop(self);

    // 释放资源
    ai_model_destroy(srv->ai_model);
    pthread_mutex_destroy(&srv->state_mutex);
    srv->base.state = SRV_STATE_IDLE;

    LOG_I("%s: deinit success", MODULE_NAME);
    return 0;
}

// ==========================================================================
// 7. 事件处理（系统指令）
// ==========================================================================
static void face_detect_event_handle(void *self, uint32_t event_id, void *data)
{
    face_detect_srv_t *srv = (face_detect_srv_t *)self;
    if (!srv) return;

    switch (event_id) {
        case EVENT_TYPE_SYS_PAUSE:
            face_detect_pause(self);
            break;
        case EVENT_TYPE_SYS_RESUME:
            face_detect_resume(self);
            break;
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            face_detect_stop(self);
            break;
        default:
            break;
    }
}

// ==========================================================================
// 自动初始化（main自动加载）
// ==========================================================================
#include "initcall.h"
static int _face_detect_auto_init(void)
{
    LOG_I("%s: auto init done", MODULE_NAME);
    return 0;
}
MODULE_INIT(_face_detect_auto_init);