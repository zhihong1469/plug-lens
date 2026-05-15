/* SPDX-License-Identifier: MIT */
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "frame_link.h"
#include "vision_ai_config.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

// ==========================================================================
// 全局宏定义（文件私有化，标注来源，方便代码巡查）
// 来源：common\configs\vision_ai_config.h
// ==========================================================================
#define MODULE_NAME           "CAPTURE"
#define MODULE_TAG            "[CAPTURE]"

// 系统总线名称（全局约定）
#define SYS_EVENT_BUS_NAME    "sys_event"    // 系统事件总线
#define VIDEO_DATA_BUS_NAME   "video"        // 采集服务自主创建的数据总线

// 采集配置（来源：vision_ai_config.h）
#define FRAME_CAPTURE_FPS         CONFIG_CAPTURE_FPS
#define FRAME_CAPTURE_HEIGHT      CONFIG_CAPTURE_HEIGHT
#define FRAME_CAPTURE_WIDTH       CONFIG_CAPTURE_WIDTH

// 服务私有配置
#define CAP_FRAME_WAIT_US         20000   // 20ms 取帧等待
#define CAP_FPS_INTERVAL_MS       1000    // FPS上报间隔
#define FRAME_POOL_SIZE           10      // 帧内存池大小
#define FRAME_QUEUE_SIZE          4       // 帧队列大小

// ==========================================================================
// 采集服务 私有结构体（静态单例，外部完全不可见）
// ==========================================================================
typedef struct {
    // 硬件帧链路（服务初始化创建）
    frame_link_handle_t     frame_link;

    // 工作线程
    pthread_t               work_thread;
    bool                    thread_running;
    bool                    is_paused;
    pthread_mutex_t         lock;

    // 视频配置
    uint32_t                width;
    uint32_t                height;
    uint32_t                fps;
    frame_format_t          format;

    // 统计信息
    uint64_t                frame_count;
    uint64_t                last_fps_ts;

    // 事件订阅ID（服务订阅创建）
    int                     evt_sub_id;
} capture_srv_t;

// 静态单例（服务自治，无对外暴露）
static capture_srv_t s_capture;

// ==========================================================================
// 内部工具函数：线程安全加锁/解锁
// ==========================================================================
static inline void _capture_lock(void) {
    pthread_mutex_lock(&s_capture.lock);
}

static inline void _capture_unlock(void) {
    pthread_mutex_unlock(&s_capture.lock);
}

// ==========================================================================
// 【核心】服务统一清理函数：释放所有创建的资源（成对释放，无泄漏）
// 启动了什么 → 释放什么：线程、帧链路、数据总线、锁、事件订阅
// ==========================================================================
static void capture_srv_cleanup(void)
{
    capture_srv_t *srv = &s_capture;

    LOG_W(MODULE_TAG " 开始执行全量资源释放...");

    // 1. 停止工作线程（最优先，防止线程继续访问资源）
    srv->thread_running = false;
    srv->is_paused = true;
    if (srv->work_thread > 0) {
        pthread_join(srv->work_thread, NULL);
        LOG_I(MODULE_TAG " 工作线程已安全退出");
        srv->work_thread = 0;
    }

    // 2. 取消系统事件订阅
    if (srv->evt_sub_id >= 0) {
        event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->evt_sub_id);
        srv->evt_sub_id = -1;
        LOG_I(MODULE_TAG " 事件订阅已取消");
    }

    // 3. 销毁帧链路（服务初始化创建）
    if (srv->frame_link) {
        frame_link_deinit(srv->frame_link);
        srv->frame_link = NULL;
        LOG_I(MODULE_TAG " 帧链路已销毁");
    }

    // 4. 销毁自主创建的 video 数据总线（核心生产者责任）
    data_bus_deinit(VIDEO_DATA_BUS_NAME);
    LOG_I(MODULE_TAG " video 数据总线已销毁");

    // 5. 销毁线程互斥锁
    pthread_mutex_destroy(&srv->lock);
    LOG_I(MODULE_TAG " 线程锁已销毁");

    // 6. 发布服务停止事件
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG " 所有资源释放完成，服务已安全退出");
}

// ==========================================================================
// 事件总线回调：响应系统指令 + 【关键】系统错误/关机时自动释放资源
// ==========================================================================
static void _capture_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    capture_srv_t *srv = &s_capture;

    switch (event->type) {
        // 系统核心就绪
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG " 收到系统就绪事件，服务准备完成");
            break;

        // 系统暂停
        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG " 收到暂停指令");
            srv->is_paused = true;
            break;

        // 系统恢复
        case EVENT_TYPE_SYS_RESUME:
            LOG_I(MODULE_TAG " 收到恢复指令");
            srv->is_paused = false;
            break;

        // 系统正常停止/关机 → 安全释放资源
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            LOG_I(MODULE_TAG " 收到系统关机/停止指令，执行资源清理");
            capture_srv_cleanup();
            break;

        // 【关键】系统致命错误 → 立即强制释放所有资源，安全退出
        case EVENT_TYPE_SYS_ERROR:
            LOG_E(MODULE_TAG " 收到系统致命错误指令，强制清理所有资源！");
            capture_srv_cleanup();
            break;

        default:
            break;
    }
}

// ==========================================================================
// 工作线程：采集核心 → 帧链路取帧 → 发布到 video 数据总线
// ==========================================================================
static void *capture_work_thread(void *arg)
{
    (void)arg;
    capture_srv_t *srv = &s_capture;
    frame_t *frame = NULL;
    data_bus_item_handle_t data_item = NULL;
    uint64_t current_ts;
    struct timeval tv;

    LOG_I(MODULE_TAG " 工作线程启动，开始采集视频帧");

    while (srv->thread_running) {
        // 暂停状态：低功耗等待
        if (srv->is_paused) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        // 1. 从帧链路非阻塞获取一帧
        if (frame_link_dequeue_frame(srv->frame_link, &frame) != 0 || !frame) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        // 2. 计算RGB帧大小
        size_t frame_size = frame->width * frame->height * 3;

        // 3. 数据总线申请内存
        if (data_bus_alloc(VIDEO_DATA_BUS_NAME,
                           DATA_TYPE_VIDEO_FRAME_RGB,
                           frame_size,
                           MODULE_NAME,
                           &data_item) == 0)
        {
            // 4. 拷贝帧数据
            void *w_buf = data_bus_get_writable_ptr(data_item);
            memcpy(w_buf, frame->data, frame_size);

            // 5. 发布数据 + 释放引用
            data_bus_publish(VIDEO_DATA_BUS_NAME, data_item);
            data_bus_release(data_item);

            // 6. 发布帧就绪事件（通知人脸检测等模块）
            event_bus_publish_simple(SYS_EVENT_BUS_NAME,
                                     EVENT_TYPE_CAPTURE_FRAME_READY,
                                     MODULE_NAME);
        }

        // 7. 归还帧到内存池
        frame_link_release_frame(srv->frame_link, frame);
        srv->frame_count++;

        // 8. FPS定时上报
        gettimeofday(&tv, NULL);
        current_ts = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        if (current_ts - srv->last_fps_ts >= CAP_FPS_INTERVAL_MS) {
            srv->last_fps_ts = current_ts;
            LOG_D(MODULE_TAG " 采集FPS: %llu", srv->frame_count);
            srv->frame_count = 0;
        }
    }

    LOG_I(MODULE_TAG " 工作线程退出");
    return NULL;
}

// ==========================================================================
// 服务初始化（自动调用）
// ==========================================================================
static int capture_srv_init(void)
{
    capture_srv_t *srv = &s_capture;
    memset(srv, 0, sizeof(capture_srv_t));

    // 1. 初始化线程锁
    pthread_mutex_init(&srv->lock, NULL);
    srv->evt_sub_id = -1;
    srv->format = FRAME_FMT_RGB888;

    // 2. 加载全局视频配置
    srv->width  = FRAME_CAPTURE_WIDTH;
    srv->height = FRAME_CAPTURE_HEIGHT;
    srv->fps    = FRAME_CAPTURE_FPS;

    // 3. 初始化帧链路（内存池+队列）
    frame_link_config_t fl_cfg = {
        .max_frame_size = srv->width * srv->height * 3,
        .pool_capacity = FRAME_POOL_SIZE,
        .queue_capacity = FRAME_QUEUE_SIZE
    };
    if (frame_link_init(&fl_cfg, &srv->frame_link) != 0) {
        LOG_E(MODULE_TAG " 帧链路初始化失败");
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 4. 【核心】初始化自主创建的 video 数据总线
    data_bus_config_t bus_cfg = {
        .max_items = 8,
        .max_item_size = srv->width * srv->height * 3,
        .max_subscribers = 8,
        .name = VIDEO_DATA_BUS_NAME
    };
    if (data_bus_init(&bus_cfg) != 0) {
        LOG_E(MODULE_TAG " video数据总线初始化失败");
        frame_link_deinit(srv->frame_link);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 5. 订阅系统事件
    event_subscriber_t evt_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _capture_event_cb,
        .user_data = srv
    };
    srv->evt_sub_id = event_bus_subscribe(SYS_EVENT_BUS_NAME, &evt_sub);
    if (srv->evt_sub_id < 0) {
        LOG_E(MODULE_TAG " 订阅事件总线失败");
        data_bus_deinit(VIDEO_DATA_BUS_NAME);
        frame_link_deinit(srv->frame_link);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    LOG_I(MODULE_TAG " 服务初始化完成 [%ux%u@%u FPS]",
          srv->width, srv->height, srv->fps);
    return 0;
}

// ==========================================================================
// 服务启动（自动调用）
// ==========================================================================
static int capture_srv_start(void)
{
    capture_srv_t *srv = &s_capture;

    // 1. 启动工作线程
    srv->thread_running = true;
    srv->is_paused = false;
    if (pthread_create(&srv->work_thread, NULL, capture_work_thread, NULL) != 0) {
        LOG_E(MODULE_TAG " 创建工作线程失败");
        srv->thread_running = false;
        return -1;
    }

    // 2. 发布服务状态事件
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_READY, MODULE_NAME);
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_RUNNING, MODULE_NAME);

    LOG_I(MODULE_TAG " 服务启动成功，开始发布视频帧");
    return 0;
}

// ==========================================================================
// 内核式自动初始化 + 启动（main自动调用）
// ==========================================================================
#include "initcall.h"
static int _capture_auto_init(void)
{
    // 1. 初始化服务
    if (capture_srv_init() != 0) {
        return -1;
    }

    // 2. 自动启动服务
    if (capture_srv_start() != 0) {
        capture_srv_cleanup();
        return -1;
    }

    LOG_I(MODULE_TAG " 自动加载并启动完成");
    return 0;
}
// 注册到系统初始化段
MODULE_INIT(_capture_auto_init);