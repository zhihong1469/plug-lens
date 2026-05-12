#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include "data_bus.h"
#include "service_base.h"
#include "frame_link.h"
#include "event_bus.h"
#include "log.h"
#include "capture_srv.h"

/* ====================== 配置宏定义 ====================== */
#define CAPTURE_SRV_NAME         "capture_service"   /* 服务名称 */
#define CAPTURE_WIDTH            640                 /* 采集宽度 */
#define CAPTURE_HEIGHT           480                 /* 采集高度 */
#define CAPTURE_FPS              30                  /* 采集帧率 */
#define CAPTURE_THREAD_STACK      8192                /* 线程栈大小 */

/* ====================== 私有结构体（纯C继承） ====================== */
/**
 * @brief 采集服务私有结构体（第一个成员为基类，实现继承）
 * @note 所有成员均为私有，static修饰，无全局变量
 */
typedef struct {
    service_base_t          base;           /* 【强制】基类成员，首地址 */
    pthread_t               thread;         /* 工作线程句柄 */
    frame_link_handle_t     frame_link;     /* 帧链路句柄 */
    data_bus_handle_t       data_bus;       /* 数据总线句柄 */
    event_bus_handle_t      event_bus;      /* 事件总线句柄 */

    /* 私有配置 */
    uint32_t                width;
    uint32_t                height;
    uint32_t                fps;

    /* 线程安全标记 */
    atomic_bool             run_flag;       /* 运行标记（原子操作） */
    atomic_bool             pause_flag;     /* 暂停标记（原子操作） */

    /* 总线订阅ID */
    int                     event_sub_id;   /* 事件总线订阅ID */
} capture_srv_t;

/* ====================== 私有函数声明 ====================== */
static int  capture_srv_init(void *self);
static int  capture_srv_start(void *self);
static int  capture_srv_pause(void *self);
static int  capture_srv_resume(void *self);
static int  capture_srv_stop(void *self);
static int  capture_srv_deinit(void *self);
static void capture_srv_event_handle(void *self, uint32_t event_id, void *data);
static void *capture_srv_worker(void *arg);
static void capture_srv_event_cb(const event_t *event, void *user_data);

/* ====================== 服务操作表（多态绑定） ====================== */
/**
 * @brief 【强制】实现基类所有7个接口
 */
static const service_ops_t g_capture_srv_ops = {
    .init        = capture_srv_init,
    .start       = capture_srv_start,
    .pause       = capture_srv_pause,
    .resume      = capture_srv_resume,
    .stop        = capture_srv_stop,
    .deinit      = capture_srv_deinit,
    .event_handle= capture_srv_event_handle,
};

/* ====================== 静态服务实例（唯一实例） ====================== */
static capture_srv_t g_capture_srv = {
    .base = {
        .state = SRV_STATE_IDLE,
        .ops   = &g_capture_srv_ops,
        .name  = CAPTURE_SRV_NAME,
    },
    .frame_link = NULL,
    .data_bus   = NULL,
    .event_bus  = NULL,
    .width      = CAPTURE_WIDTH,
    .height     = CAPTURE_HEIGHT,
    .fps        = CAPTURE_FPS,
    .run_flag   = ATOMIC_VAR_INIT(false),
    .pause_flag = ATOMIC_VAR_INIT(false),
    .event_sub_id = -1,
};

/* ====================== 对外接口 ====================== */
service_base_t *capture_srv_get_instance(void)
{
    return &g_capture_srv.base;
}

/* ====================== 生命周期接口实现 ====================== */
/**
 * @brief  初始化：资源分配、总线订阅、状态置INIT
 * @note   不启动线程、不操作硬件
 */
static int capture_srv_init(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    int ret = 0;

    /* 状态校验：只能从IDLE状态进入初始化 */
    if (srv->base.state != SRV_STATE_IDLE) {
        LOG_E("capture srv init failed, invalid state: %d", srv->base.state);
        return -1;
    }

    /* 1. 获取全局总线句柄（项目全局单例，由应用层初始化） */
    // extern data_bus_handle_t  g_data_bus;
    // extern event_bus_handle_t g_event_bus;
    // extern frame_link_handle_t g_frame_link;
    // srv->data_bus   = g_data_bus;
    // srv->event_bus  = g_event_bus;
    // srv->frame_link = g_frame_link;

    /* 2. 订阅全局系统事件（所有服务必须订阅） */
    event_subscriber_t sub = {
        .event_type = EVENT_TYPE_INVALID,  /* 订阅所有事件，内部过滤处理 */
        .callback   = capture_srv_event_cb,
        .user_data  = srv,
    };
    srv->event_sub_id = event_bus_subscribe(srv->event_bus, &sub);
    if (srv->event_sub_id < 0) {
        LOG_E("capture srv subscribe event bus failed");
        return -2;
    }

    /* 3. 初始化线程安全标记 */
    atomic_store(&srv->run_flag, false);
    atomic_store(&srv->pause_flag, false);

    /* 4. 状态切换：IDLE → INIT */
    srv->base.state = SRV_STATE_INIT;
    LOG_I("capture service initialized successfully");

    return 0;
}

/**
 * @brief  启动：创建工作线程、状态置RUNNING
 */
static int capture_srv_start(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    int ret = 0;

    /* 状态校验：只能从INIT状态进入运行 */
    if (srv->base.state != SRV_STATE_INIT) {
        LOG_E("capture srv start failed, invalid state: %d", srv->base.state);
        return -1;
    }

    /* 启动工作线程 */
    atomic_store(&srv->run_flag, true);
    ret = pthread_create(&srv->thread, NULL, capture_srv_worker, srv);
    if (ret != 0) {
        atomic_store(&srv->run_flag, false);
        LOG_E("capture srv create worker thread failed: %d", ret);
        return -2;
    }

    /* 发布采集开始事件 */
    event_bus_publish_simple(srv->event_bus, EVENT_TYPE_CAP_START, CAPTURE_SRV_NAME);

    /* 状态切换：INIT → RUNNING */
    srv->base.state = SRV_STATE_RUNNING;
    LOG_I("capture service started successfully");

    return 0;
}

/**
 * @brief  暂停：挂起采集、状态置PAUSE
 */
static int capture_srv_pause(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;

    /* 状态校验：只能从RUNNING状态进入暂停 */
    if (srv->base.state != SRV_STATE_RUNNING) {
        LOG_W("capture srv pause failed, invalid state: %d", srv->base.state);
        return -1;
    }

    atomic_store(&srv->pause_flag, true);

    /* 状态切换：RUNNING → PAUSE */
    srv->base.state = SRV_STATE_PAUSE;
    LOG_I("capture service paused");

    return 0;
}

/**
 * @brief  恢复：继续采集、状态切RUNNING
 */
static int capture_srv_resume(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;

    /* 状态校验：只能从PAUSE状态恢复 */
    if (srv->base.state != SRV_STATE_PAUSE) {
        LOG_W("capture srv resume failed, invalid state: %d", srv->base.state);
        return -1;
    }

    atomic_store(&srv->pause_flag, false);

    /* 状态切换：PAUSE → RUNNING */
    srv->base.state = SRV_STATE_RUNNING;
    LOG_I("capture service resumed");

    return 0;
}

/**
 * @brief  停止：销毁线程、状态置STOP
 */
static int capture_srv_stop(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;

    /* 状态校验：只能从RUNNING或PAUSE状态停止 */
    if (srv->base.state != SRV_STATE_RUNNING && srv->base.state != SRV_STATE_PAUSE) {
        LOG_W("capture srv stop failed, invalid state: %d", srv->base.state);
        return -1;
    }

    /* 停止工作线程 */
    atomic_store(&srv->run_flag, false);
    pthread_join(srv->thread, NULL);

    /* 发布采集停止事件 */
    event_bus_publish_simple(srv->event_bus, EVENT_TYPE_CAP_STOP, CAPTURE_SRV_NAME);

    /* 状态切换：RUNNING/PAUSE → STOP */
    srv->base.state = SRV_STATE_STOP;
    LOG_I("capture service stopped");

    return 0;
}

/**
 * @brief  反初始化：释放所有资源、状态置IDLE
 * @note   资源成对释放，无内存泄漏
 */
static int capture_srv_deinit(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;

    /* 状态校验：只能从STOP状态销毁 */
    if (srv->base.state != SRV_STATE_STOP) {
        LOG_E("capture srv deinit failed, invalid state: %d", srv->base.state);
        return -1;
    }

    /* 取消事件总线订阅 */
    if (srv->event_sub_id >= 0) {
        event_bus_unsubscribe(srv->event_bus, srv->event_sub_id);
        srv->event_sub_id = -1;
    }

    /* 清空句柄 */
    srv->frame_link = NULL;
    srv->data_bus   = NULL;
    srv->event_bus  = NULL;

    /* 状态切换：STOP → IDLE */
    srv->base.state = SRV_STATE_IDLE;
    LOG_I("capture service deinitialized successfully");

    return 0;
}

/**
 * @brief  事件处理：转发全局命令到服务接口
 * @note   所有事件都在这里统一处理，内部过滤不需要的事件
 */
static void capture_srv_event_handle(void *self, uint32_t event_id, void *data)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    event_type_t type = (event_type_t)event_id;

    switch (type) {
        /* 全局控制事件 */
        case EVENT_TYPE_SYS_PAUSE:
            capture_srv_pause(srv);
            break;
        case EVENT_TYPE_SYS_RESUME:
            capture_srv_resume(srv);
            break;
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            capture_srv_stop(srv);
            capture_srv_deinit(srv);
            break;

        /* 采集服务私有事件（如果有需要订阅的） */
        // case EVENT_TYPE_XXX:
        //     handle_xxx_event(srv, data);
        //     break;

        /* 忽略其他不关心的事件 */
        default:
            LOG_D("capture srv ignore event: 0x%04X", type);
            break;
    }
}

/* ====================== 核心工作线程 ====================== */
/**
 * @brief  采集服务工作线程：帧读取 → 零拷贝发布 → 事件通知
 * @note   唯一数据生产者，仅向数据总线写入，服务间零耦合
 */
static void *capture_srv_worker(void *arg)
{
    capture_srv_t *srv = (capture_srv_t *)arg;
    frame_t *frame = NULL;
    data_bus_item_handle_t data_item = NULL;
    int ret = 0;

    LOG_I("capture service worker thread started");

    while (atomic_load(&srv->run_flag)) {
        /* 暂停状态：休眠等待，不占用CPU */
        if (atomic_load(&srv->pause_flag)) {
            usleep(10000);
            continue;
        }

        /* 1. 从帧链路获取摄像头帧（底层接口，无硬件直接操作） */
        ret = frame_link_dequeue_frame(srv->frame_link, &frame);
        if (ret != 0 || frame == NULL) {
            usleep(1000);
            continue;
        }

        /* 2. 数据总线：申请内存（零拷贝，引用计数管理） */
        ret = data_bus_alloc(srv->data_bus,
                             DATA_TYPE_VIDEO_FRAME_RGB,
                             frame->width * frame->height * 3,
                             CAPTURE_SRV_NAME,
                             &data_item);
        if (ret != 0) {
            LOG_E("capture srv data bus alloc failed, ret: %d", ret);
            frame_link_release_frame(srv->frame_link, frame);
            srv->base.state = SRV_STATE_ERROR;
            event_bus_publish_simple(srv->event_bus, EVENT_TYPE_CAP_ERROR, CAPTURE_SRV_NAME);
            break;
        }

        /* 3. 拷贝帧数据（仅生产者可写入数据总线） */
        void *w_ptr = data_bus_get_writable_ptr(data_item);
        memcpy(w_ptr, frame->data, frame->width * frame->height * 3);

        /* 4. 发布数据到总线（自动通知所有订阅的消费者） */
        data_bus_publish(srv->data_bus, data_item);

        /* 5. 发布事件：帧就绪通知（小数据走事件总线） */
        event_bus_publish_simple(srv->event_bus,
                                 EVENT_TYPE_CAP_FRAME_READY,
                                 CAPTURE_SRV_NAME);

        /* 6. 释放资源（引用计数-1，没人用自动回收） */
        frame_link_release_frame(srv->frame_link, frame);
        data_bus_release(data_item);

        /* 帧率控制 */
        usleep(1000000 / srv->fps);
    }

    LOG_I("capture service worker thread exited gracefully");
    return NULL;
}

/* ====================== 事件总线回调 ====================== */
/**
 * @brief  事件总线统一回调入口，转发到服务事件处理函数
 */
static void capture_srv_event_cb(const event_t *event, void *user_data)
{
    capture_srv_t *srv = (capture_srv_t *)user_data;
    capture_srv_event_handle(srv, event->type, event->data);
}