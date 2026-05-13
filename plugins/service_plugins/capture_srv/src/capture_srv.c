#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "vision_ai_config.h"
#include "capture_srv.h"
#include "frame_link.h"
#include "data_bus.h"
#include "event_bus.h"
#include "log.h"

// ==========================================================================
// 配置宏（与项目配置对齐）
// ==========================================================================
#define CAP_SRV_NAME         "capture_srv"
#define CAP_FRAME_QUEUE_WAIT 10000  // 取帧超时(us)
#define CAP_FPS_REPORT_INTERVAL 1000 // FPS上报间隔(ms)

// ==========================================================================
// 采集服务 私有结构体（纯C继承：首成员为service_base_t）
// 所有成员static修饰，无全局变量，无公共暴露
// ==========================================================================
typedef struct {
    // 基类（必须放在第一个，实现OOP继承）
    service_base_t base;

    // 工作线程
    pthread_t work_thread;
    volatile bool is_running;  // 线程运行标记（原子性）

    // 核心句柄
    frame_link_handle_t frame_link;       // 帧链路句柄
    data_bus_handle_t data_bus;            // 数据总线句柄（唯一生产者）
    event_bus_handle_t event_bus;          // 事件总线句柄
    int event_sub_id;                      // 事件订阅ID

    // 私有配置
    uint32_t width;
    uint32_t height;
    uint32_t fps;

    // 状态与统计
    uint64_t frame_count;
    uint64_t last_report_ts;
} capture_srv_t;

// 静态服务实例（唯一实例，对外隐藏）
static capture_srv_t s_capture_srv;

// ==========================================================================
// 前置声明
// ==========================================================================
static int  capture_srv_init(void *self);
static int  capture_srv_start(void *self);
static int  capture_srv_pause(void *self);
static int  capture_srv_resume(void *self);
static int  capture_srv_stop(void *self);
static int  capture_srv_deinit(void *self);
static void capture_srv_event_handle(void *self, uint32_t event_id, void *data);
static void *capture_srv_work_thread(void *arg);

// ==========================================================================
// 服务操作表（绑定7个生命周期接口，基类多态调用）
// ==========================================================================
static const service_ops_t s_capture_srv_ops = {
    .init        = capture_srv_init,
    .start       = capture_srv_start,
    .pause       = capture_srv_pause,
    .resume      = capture_srv_resume,
    .stop        = capture_srv_stop,
    .deinit      = capture_srv_deinit,
    .event_handle = capture_srv_event_handle
};

// ==========================================================================
// 对外接口：获取服务基类指针
// ==========================================================================
service_base_t *capture_srv_get_instance(void)
{
    return &s_capture_srv.base;
}

// ==========================================================================
// 事件总线：全局事件回调（转发给服务事件处理函数）
// ==========================================================================
static void capture_srv_event_cb(const event_t *event, void *user_data)
{
    capture_srv_t *srv = (capture_srv_t *)user_data;
    if (!srv || !event) return;

    capture_srv_event_handle(srv, event->type, event->data);
}

// ==========================================================================
// 1. 初始化接口（IDLE → INIT）
// 功能：初始化资源、订阅事件、绑定总线，不启动线程
// ==========================================================================
static int capture_srv_init(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    if (!srv) return -1;

    // 基类初始化
    srv->base.state = SRV_STATE_INIT;
    srv->base.ops   = &s_capture_srv_ops;
    srv->base.name  = CAP_SRV_NAME;

    // 私有成员初始化
    srv->is_running = false;
    srv->work_thread = 0;
    srv->event_sub_id = -1;
    srv->frame_count = 0;
    srv->last_report_ts = 0;

    // 加载配置
    srv->width  = CONFIG_CAPTURE_WIDTH;
    srv->height = CONFIG_CAPTURE_HEIGHT;
    srv->fps    = CONFIG_CAPTURE_FPS;

    // 获取全局总线句柄（由应用层统一初始化）
    srv->data_bus = g_data_bus;
    srv->event_bus = g_event_bus;
    srv->frame_link = g_frame_link;

    // 订阅全局控制事件（所有服务必须订阅）
    event_subscriber_t sub = {
        .event_type = EVENT_TYPE_INVALID,  // 订阅所有系统事件
        .callback = capture_srv_event_cb,
        .user_data = srv
    };
    srv->event_sub_id = event_bus_subscribe(srv->event_bus, &sub);
    if (srv->event_sub_id < 0) {
        LOG_E("capture srv subscribe event failed");
        return -2;
    }

    LOG_I("capture srv init success, %ux%u@%u fps",
          srv->width, srv->height, srv->fps);
    return 0;
}

// ==========================================================================
// 2. 启动接口（INIT → RUNNING）
// 功能：创建工作线程，启动采集逻辑
// ==========================================================================
static int capture_srv_start(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    if (!srv || srv->base.state != SRV_STATE_INIT) return -1;

    // 启动工作线程
    srv->is_running = true;
    int ret = pthread_create(&srv->work_thread, NULL,
                             capture_srv_work_thread, srv);
    if (ret != 0) {
        LOG_E("create capture thread failed: %d", ret);
        srv->is_running = false;
        return -2;
    }

    // 状态切换 + 发布启动事件
    srv->base.state = SRV_STATE_RUNNING;
    event_bus_publish_simple(srv->event_bus, EVENT_TYPE_CAP_START, CAP_SRV_NAME);
    LOG_I("capture srv started");

    return 0;
}

// ==========================================================================
// 3. 暂停接口（RUNNING → PAUSE）
// ==========================================================================
static int capture_srv_pause(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    if (!srv || srv->base.state != SRV_STATE_RUNNING) return -1;

    srv->base.state = SRV_STATE_PAUSE;
    LOG_I("capture srv paused");
    return 0;
}

// ==========================================================================
// 4. 恢复接口（PAUSE → RUNNING）
// ==========================================================================
static int capture_srv_resume(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    if (!srv || srv->base.state != SRV_STATE_PAUSE) return -1;

    srv->base.state = SRV_STATE_RUNNING;
    LOG_I("capture srv resumed");
    return 0;
}

// ==========================================================================
// 5. 停止接口（RUNNING/PAUSE → STOP）
// 功能：停止线程，不释放资源
// ==========================================================================
static int capture_srv_stop(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    if (!srv || srv->base.state == SRV_STATE_STOP) return 0;

    // 停止线程
    srv->is_running = false;
    if (srv->work_thread > 0) {
        pthread_join(srv->work_thread, NULL);
        srv->work_thread = 0;
    }

    // 状态切换 + 发布停止事件
    srv->base.state = SRV_STATE_STOP;
    event_bus_publish_simple(srv->event_bus, EVENT_TYPE_CAP_STOP, CAP_SRV_NAME);
    LOG_I("capture srv stopped");

    return 0;
}

// ==========================================================================
// 6. 销毁接口（STOP → IDLE）
// 功能：释放所有资源，成对释放
// ==========================================================================
static int capture_srv_deinit(void *self)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    if (!srv) return -1;

    // 确保已停止
    capture_srv_stop(self);

    // 取消事件订阅
    if (srv->event_sub_id >= 0) {
        event_bus_unsubscribe(srv->event_bus, srv->event_sub_id);
        srv->event_sub_id = -1;
    }

    // 状态重置
    srv->base.state = SRV_STATE_IDLE;
    LOG_I("capture srv deinit success");

    return 0;
}

// ==========================================================================
// 7. 事件处理接口（处理全局控制命令）
// ==========================================================================
static void capture_srv_event_handle(void *self, uint32_t event_id, void *data)
{
    capture_srv_t *srv = (capture_srv_t *)self;
    if (!srv) return;

    switch (event_id) {
        // 全局暂停
        case EVENT_TYPE_SYS_PAUSE:
            capture_srv_pause(self);
            break;
        // 全局恢复
        case EVENT_TYPE_SYS_RESUME:
            capture_srv_resume(self);
            break;
        // 全局停止/关机
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            capture_srv_stop(self);
            break;
        default:
            break;
    }
}

// ==========================================================================
// 工作线程：核心业务逻辑（从frame_link取帧 → 发布到数据总线）
// 严格遵守：唯一生产者，零拷贝，不修改硬件
// ==========================================================================
static void *capture_srv_work_thread(void *arg)
{
    capture_srv_t *srv = (capture_srv_t *)arg;
    frame_t *frame = NULL;
    data_bus_item_handle_t item = NULL;
    uint64_t current_ts;

    LOG_I("capture work thread start");

    while (srv->is_running) {
        // 暂停状态：空循环
        if (srv->base.state == SRV_STATE_PAUSE) {
            usleep(10000);
            continue;
        }

        // 1. 从frame_link非阻塞取帧
        int ret = frame_link_dequeue_frame(srv->frame_link, &frame);
        if (ret != 0 || !frame) {
            usleep(CAP_FRAME_QUEUE_WAIT);
            continue;
        }

        // 2. 数据总线：申请内存（零拷贝）
        size_t frame_size = frame->width * frame->height * 3; // RGB888
        ret = data_bus_alloc(srv->data_bus, DATA_TYPE_VIDEO_FRAME_RGB,
                             frame_size, CAP_SRV_NAME, &item);
        if (ret != 0 || !item) {
            frame_link_release_frame(srv->frame_link, frame);
            continue;
        }

        // 3. 拷贝帧数据（生产者唯一写入）
        void *w_ptr = data_bus_get_writable_ptr(item);
        memcpy(w_ptr, frame->data, frame_size);

        // 4. 发布数据到总线（通知所有消费者）
        data_bus_publish(srv->data_bus, item);
        data_bus_release(item);  // 释放引用

        // 5. 归还帧到内存池
        frame_link_release_frame(srv->frame_link, frame);
        srv->frame_count++;

        // 6. 发布帧就绪事件
        event_bus_publish_simple(srv->event_bus,
                                 EVENT_TYPE_CAP_FRAME_READY, CAP_SRV_NAME);

        // 7. FPS定时上报
        current_ts = srv->frame_count * 1000 / srv->fps;
        if (current_ts - srv->last_report_ts >= CAP_FPS_REPORT_INTERVAL) {
            event_bus_publish_simple(srv->event_bus,
                                     EVENT_TYPE_CAP_FPS_REPORT, CAP_SRV_NAME);
            srv->last_report_ts = current_ts;
        }
    }

    LOG_I("capture work thread exit");
    return NULL;
}