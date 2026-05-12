#include "face_detect_srv.h"
#include "ai_model_mnn.hpp"
#include "log.h"
#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MODULE_NAME     "FACE_DETECT"
#define MAX_FACES       10

// 服务私有结构体（完全封装，外部不可见）
struct face_detect_srv {
    // 总线句柄
    event_bus_handle_t      evt_bus;
    data_bus_handle_t       data_bus;

    // AI模型
    ai_model_handle_t      *ai_model;
    FaceInfo_C              faces[MAX_FACES];
    int                     face_num;

    // 服务状态
    face_srv_state_t        state;
    pthread_mutex_t         state_mutex;
    bool                    thread_running;
    pthread_t               work_thread;

    // 订阅ID（用于取消订阅）
    int                     evt_sub_id;
    data_bus_subscription_handle_t data_sub;
};

// ==============================================
// 工具函数：状态锁
// ==============================================
static void _lock_state(face_detect_srv_handle_t *srv) {
    pthread_mutex_lock(&srv->state_mutex);
}

static void _unlock_state(face_detect_srv_handle_t *srv) {
    pthread_mutex_unlock(&srv->state_mutex);
}

// ==============================================
// 数据总线回调：接收视频帧 → AI推理 → 发布结果
// ==============================================
static void _data_bus_cb(data_bus_item_handle_t item, void *user_data)
{
    face_detect_srv_handle_t *srv = (face_detect_srv_handle_t *)user_data;
    if (!srv || srv->state != FACE_SRV_STATE_RUNNING) {
        data_bus_release(item);
        return;
    }

    // 1. 获取只读视频数据
    const void *data = data_bus_get_readonly_ptr(item);
    data_bus_item_info_t info;
    data_bus_get_item_info(item, &info);

    // 2. 仅处理RGB帧
    if (info.type != DATA_TYPE_VIDEO_FRAME_RGB) {
        data_bus_release(item);
        return;
    }

    // 3. AI推理（YUYV/RGB自适应）
    srv->face_num = 0;
    int ret = ai_model_mnn_infer_yuyv((const uint8_t *)data, 
                                     640, 480,  // 相机分辨率（可配置化）
                                     srv->faces, 
                                     MAX_FACES, 
                                     &srv->face_num);
    if (ret != 0 || srv->face_num <= 0) {
        data_bus_release(item);
        return;
    }

    LOG_D("%s: Detect %d faces", MODULE_NAME, srv->face_num);

    // 4. 发布AI结果到数据总线
    data_bus_item_handle_t ai_item = NULL;
    if (data_bus_alloc(srv->data_bus,
                       DATA_TYPE_AI_RESULT,
                       sizeof(FaceInfo_C) * srv->face_num,
                       MODULE_NAME,
                       &ai_item) == 0)
    {
        void *w_ptr = data_bus_get_writable_ptr(ai_item);
        memcpy(w_ptr, srv->faces, sizeof(FaceInfo_C) * srv->face_num);
        data_bus_publish(srv->data_bus, ai_item);
    }

    data_bus_release(item);
}

// ==============================================
// 事件总线回调：接收系统控制指令
// ==============================================
static void _event_bus_cb(const event_t *event, void *user_data)
{
    face_detect_srv_handle_t *srv = (face_detect_srv_handle_t *)user_data;
    if (!srv || !event) return;

    _lock_state(srv);
    switch (event->type) {
        case EVENT_TYPE_SYS_RESUME:
            if (srv->state == FACE_SRV_STATE_PAUSED) {
                srv->state = FACE_SRV_STATE_RUNNING;
                LOG_I("%s: Resumed", MODULE_NAME);
                event_bus_publish_simple(srv->evt_bus, EVENT_TYPE_MOD_RUNNING, MODULE_NAME);
            }
            break;

        case EVENT_TYPE_SYS_PAUSE:
            if (srv->state == FACE_SRV_STATE_RUNNING) {
                srv->state = FACE_SRV_STATE_PAUSED;
                LOG_I("%s: Paused", MODULE_NAME);
            }
            break;

        case EVENT_TYPE_SYS_STOP:
            srv->state = FACE_SRV_STATE_STOPPED;
            srv->thread_running = false;
            LOG_I("%s: Stopped", MODULE_NAME);
            break;

        default:
            break;
    }
    _unlock_state(srv);
}

// ==============================================
// 工作线程（空循环，事件驱动）
// ==============================================
static void *_work_thread(void *arg)
{
    face_detect_srv_handle_t *srv = (face_detect_srv_handle_t *)arg;
    while (srv->thread_running) {
        sleep(1);
    }
    return NULL;
}

// ==============================================
// 对外接口实现
// ==============================================
face_detect_srv_handle_t *face_detect_srv_create(const face_detect_cfg_t *cfg)
{
    if (!cfg || !cfg->evt_bus || !cfg->data_bus) {
        LOG_E("%s: Invalid config", MODULE_NAME);
        return NULL;
    }

    // 分配内存
    face_detect_srv_handle_t *srv = calloc(1, sizeof(struct face_detect_srv));
    if (!srv) return NULL;

    // 初始化基础成员
    srv->evt_bus = cfg->evt_bus;
    srv->data_bus = cfg->data_bus;
    srv->state = FACE_SRV_STATE_IDLE;
    pthread_mutex_init(&srv->state_mutex, NULL);

    // 初始化AI模型
    srv->ai_model = ai_model_mnn_create(&cfg->ai_cfg);
    if (!srv->ai_model) {
        LOG_E("%s: AI model create failed", MODULE_NAME);
        free(srv);
        return NULL;
    }

    LOG_I("%s: Create success", MODULE_NAME);
    return srv;
}

int face_detect_srv_start(face_detect_srv_handle_t *srv)
{
    if (!srv) return -1;

    _lock_state(srv);
    if (srv->state != FACE_SRV_STATE_IDLE) {
        _unlock_state(srv);
        return -2;
    }

    // 1. 订阅事件总线
    event_subscriber_t sub = {0};
    sub.event_type = EVENT_TYPE_INVALID;
    sub.callback = _event_bus_cb;
    sub.user_data = srv;
    srv->evt_sub_id = event_bus_subscribe(srv->evt_bus, &sub);

    // 2. 订阅数据总线（RGB视频帧）
    data_bus_subscribe(srv->data_bus,
                       DATA_TYPE_VIDEO_FRAME_RGB,
                       _data_bus_cb,
                       srv,
                       &srv->data_sub);

    // 3. 启动工作线程
    srv->thread_running = true;
    pthread_create(&srv->work_thread, NULL, _work_thread, srv);

    // 4. 更新状态
    srv->state = FACE_SRV_STATE_RUNNING;
    _unlock_state(srv);

    event_bus_publish_simple(srv->evt_bus, EVENT_TYPE_MOD_READY, MODULE_NAME);
    event_bus_publish_simple(srv->evt_bus, EVENT_TYPE_MOD_RUNNING, MODULE_NAME);
    LOG_I("%s: Started", MODULE_NAME);

    return 0;
}

int face_detect_srv_stop(face_detect_srv_handle_t *srv)
{
    if (!srv) return -1;

    _lock_state(srv);
    srv->thread_running = false;
    srv->state = FACE_SRV_STATE_STOPPED;
    _unlock_state(srv);

    // 等待线程退出
    pthread_join(srv->work_thread, NULL);

    // 取消订阅
    event_bus_unsubscribe(srv->evt_bus, srv->evt_sub_id);
    data_bus_unsubscribe(srv->data_bus, &srv->data_sub);

    event_bus_publish_simple(srv->evt_bus, EVENT_TYPE_MOD_STOPPED, MODULE_NAME);
    LOG_I("%s: Stopped", MODULE_NAME);
    return 0;
}

void face_detect_srv_destroy(face_detect_srv_handle_t **srv)
{
    if (!srv || !*srv) return;

    face_detect_srv_handle_t *tmp = *srv;
    face_detect_srv_stop(tmp);

    // 销毁AI模型
    ai_model_destroy(tmp->ai_model);
    pthread_mutex_destroy(&tmp->state_mutex);
    free(tmp);
    *srv = NULL;

    LOG_I("%s: Destroyed", MODULE_NAME);
}

face_srv_state_t face_detect_srv_get_state(face_detect_srv_handle_t *srv)
{
    return srv ? srv->state : FACE_SRV_STATE_ERROR;
}

// ==============================================
// 自动初始化（对接initcall，main自动加载）
// ==============================================
#include "initcall.h"
static void _face_detect_srv_init(void)
{
    // 自动初始化逻辑（可在app.c中调用）
    LOG_I("%s: Auto init done", MODULE_NAME);
}
module_init(_face_detect_srv_init);