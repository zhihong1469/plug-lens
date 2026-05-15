/* SPDX-License-Identifier: MIT */
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "frame_link.h"
#include "vision_ai_config.h"
#include "camera_usb.h"
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
#define SYS_EVENT_BUS_NAME        "sys_event"        // 来源：vision_ai_config.h
#define VIDEO_DATA_BUS_NAME       "video"            // 采集服务私有数据总线

// 采集核心配置（来源：vision_ai_config.h）
#define CAPTURE_DEV_PATH          CONFIG_CAPTURE_DEV_PATH    // USB摄像头设备节点
#define CAPTURE_WIDTH             CONFIG_CAPTURE_WIDTH       // 采集宽度
#define CAPTURE_HEIGHT            CONFIG_CAPTURE_HEIGHT      // 采集高度
#define CAPTURE_FPS               CONFIG_CAPTURE_FPS         // 采集帧率
#define CAPTURE_FORMAT_CFG        CONFIG_CAPTURE_FORMAT      // 0=YUYV 1=NV12 2=MJPEG
#define CAPTURE_BUF_CNT           CONFIG_CAPTURE_BUF_COUNT   // 摄像头缓冲区数量

// 帧链路配置（来源：vision_ai_config.h）
#define FRAME_LINK_POOL_SIZE       CONFIG_FRAME_LINK_POOL_SIZE
#define FRAME_LINK_QUEUE_SIZE      CONFIG_FRAME_LINK_QUEUE_SIZE

// 服务私有固定配置
#define CAP_FRAME_WAIT_US         20000   // 20ms 取帧等待
#define CAP_FPS_INTERVAL_MS       1000    // FPS上报间隔

// ==========================================================================
// 采集服务 私有结构体（静态单例，外部完全不可见）
// ==========================================================================
typedef struct {
    // 新增：USB摄像头硬件句柄（仅新增这一个成员）
    camera_base_t          *cam;
    // 原有帧链路（完整保留）
    frame_link_handle_t     frame_link;

    // 工作线程（完整保留）
    pthread_t               work_thread;
    bool                    thread_running;
    bool                    is_paused;
    bool                    is_started;       // 新增：服务启动标志，防止重复启动
    pthread_mutex_t         lock;

    // 视频配置（完整保留）
    uint32_t                width;
    uint32_t                height;
    uint32_t                fps;
    uint32_t                v4l2_fmt;   // 新增：V4L2格式

    // 统计信息（完整保留）
    uint64_t                frame_count;
    uint64_t                last_fps_ts;

    // 事件订阅ID（完整保留）
    int                     evt_sub_id;
} capture_srv_t;

// 静态单例（服务自治，无对外暴露）
static capture_srv_t s_capture;

// ==========================================================================
// 内部工具函数：线程安全加锁/解锁 + 格式转换
// ==========================================================================
static inline void _capture_lock(void) {
    pthread_mutex_lock(&s_capture.lock);
}

static inline void _capture_unlock(void) {
    pthread_mutex_unlock(&s_capture.lock);
}

// 格式转换：全局配置 → V4L2标准格式（仅内部工具）
static uint32_t _capture_get_v4l2_format(int cfg)
{
    switch (cfg) {
        case 0:  return V4L2_PIX_FMT_YUYV;
        case 1:  return V4L2_PIX_FMT_NV12;
        case 2:  return V4L2_PIX_FMT_MJPEG;
        default: return V4L2_PIX_FMT_YUYV;
    }
}

// ==========================================================================
// 【核心】服务统一清理函数：完整保留原有逻辑 + 新增摄像头销毁
// ==========================================================================
static void capture_srv_cleanup(void)
{
    capture_srv_t *srv = &s_capture;

    LOG_W(MODULE_TAG " 开始执行全量资源释放...");

    // 1. 停止工作线程（最优先）
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

    // 3. 销毁USB摄像头（新增，成对释放）
    if (srv->cam) {
        camera_usb_destroy(srv->cam);
        srv->cam = NULL;
        LOG_I(MODULE_TAG " USB摄像头已销毁");
    }

    // 4. 销毁帧链路（原有逻辑，完整保留）
    if (srv->frame_link) {
        frame_link_deinit(srv->frame_link);
        srv->frame_link = NULL;
        LOG_I(MODULE_TAG " 帧链路已销毁");
    }

    // 5. 销毁自主创建的 video 数据总线（原有逻辑）
    data_bus_deinit(VIDEO_DATA_BUS_NAME);
    LOG_I(MODULE_TAG " video 数据总线已销毁");

    // 6. 销毁线程互斥锁（原有逻辑）
    pthread_mutex_destroy(&srv->lock);
    LOG_I(MODULE_TAG " 线程锁已销毁");

    // 7. 发布服务停止事件（原有逻辑）
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG " 所有资源释放完成，服务已安全退出");
}

// ==========================================================================
// 工作线程：修复版 严格对齐你的调试代码
// 摄像头采集 → 入队FrameLink(AI消费) → 发布YUYV数据到总线
// ==========================================================================
static void *capture_work_thread(void *arg)
{
    (void)arg;
    capture_srv_t *srv = &s_capture;
    frame_t *frame = NULL;
    data_bus_item_handle_t data_item = NULL;
    uint64_t current_ts;
    struct timeval tv;
    void *cam_buf = NULL;
    size_t cam_len = 0;

    LOG_I(MODULE_TAG " 工作线程启动，开始采集视频帧");

    while (srv->thread_running) {
        if (srv->is_paused) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        // 1. 获取空闲帧
        if (frame_link_get_free_frame(srv->frame_link, &frame) != 0) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        // 2. 摄像头采集
        if (camera_get_frame(srv->cam, &cam_buf, &cam_len) != 0) {
            frame_link_return_free_frame(srv->frame_link, frame);
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        // 3. 填充帧（YUYV格式，真实大小）
        memcpy(frame->data, cam_buf, cam_len);
        frame->width  = srv->width;
        frame->height = srv->height;
        frame->format = FRAME_FMT_YUYV;
        frame->index  = srv->frame_count++;

        // 4. ✅ 入队FrameLink（AI线程从此取帧，不改动！）
        frame_link_enqueue_frame(srv->frame_link, frame);

        // 5. ✅ 发布【真实YUYV数据】到数据总线（修正格式+大小）
        size_t frame_size = srv->width * srv->height * 2; // YUYV=2字节
        if (data_bus_alloc(VIDEO_DATA_BUS_NAME,
                           DATA_TYPE_VIDEO_FRAME, // 改为通用帧，不造假
                           frame_size,
                           MODULE_NAME,
                           &data_item) == 0)
        {
            void *w_buf = data_bus_get_writable_ptr(data_item);
            memcpy(w_buf, frame->data, frame_size);
            data_bus_publish(VIDEO_DATA_BUS_NAME, data_item);
            data_bus_release(data_item);

            event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_FRAME_READY, MODULE_NAME);
        }

        // FPS统计
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
// 服务启动：**原有逻辑 + 新增摄像头启动**
// ==========================================================================
static int capture_srv_start(void)
{
    capture_srv_t *srv = &s_capture;

    // 新增：启动USB摄像头采集
    if (camera_start_capture(srv->cam) != 0) {
        LOG_E(MODULE_TAG " 启动摄像头采集失败");
        return -1;
    }

    // 原有逻辑：启动工作线程
    srv->thread_running = true;
    srv->is_paused = false;
    if (pthread_create(&srv->work_thread, NULL, capture_work_thread, NULL) != 0) {
        LOG_E(MODULE_TAG " 创建工作线程失败");
        camera_stop_capture(srv->cam);
        srv->thread_running = false;
        return -1;
    }

    // 原有逻辑：发布状态事件
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_READY, MODULE_NAME);
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_RUNNING, MODULE_NAME);

    LOG_I(MODULE_TAG " 服务启动成功，开始发布视频帧");
    return 0;
}

// ==========================================================================
// 事件总线回调：收到RESUME事件时启动服务（仅新增启动逻辑）
// ==========================================================================
static void _capture_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    capture_srv_t *srv = &s_capture;

    switch (event->type) {
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG " 收到系统就绪事件，服务准备完成");
            break;

        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG " 收到暂停指令");
            srv->is_paused = true;
            break;

        // 核心：收到恢复指令 → 启动采集服务（仅第一次执行）
        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started) {
                LOG_I(MODULE_TAG " 收到启动指令，开始初始化采集");
                capture_srv_start();
                srv->is_started = true;
            } else {
                LOG_I(MODULE_TAG " 收到恢复指令，继续采集");
                srv->is_paused = false;
            }
            break;

        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            LOG_I(MODULE_TAG " 收到系统关机/停止指令，执行资源清理");
            capture_srv_cleanup();
            break;

        case EVENT_TYPE_SYS_ERROR:
            LOG_E(MODULE_TAG " 收到系统致命错误指令，强制清理所有资源！");
            capture_srv_cleanup();
            break;

        default:
            break;
    }
}

// ==========================================================================
// 服务初始化：**完整保留原有逻辑 + 新增USB摄像头初始化**
// ==========================================================================
static int capture_srv_init(void)
{
    capture_srv_t *srv = &s_capture;
    memset(srv, 0, sizeof(capture_srv_t));

    // 1. 初始化线程锁（原有逻辑）
    pthread_mutex_init(&srv->lock, NULL);
    srv->evt_sub_id = -1;
    srv->cam = NULL;
    srv->is_started = false;  // 初始化未启动

    // 2. 加载全局视频配置（私有化宏）
    srv->width      = CAPTURE_WIDTH;
    srv->height     = CAPTURE_HEIGHT;
    srv->fps        = CAPTURE_FPS;
    srv->v4l2_fmt   = _capture_get_v4l2_format(CAPTURE_FORMAT_CFG);

    // 3. 初始化帧链路（原有逻辑，100%保留）
    frame_link_config_t fl_cfg = {
        .max_frame_size = srv->width * srv->height * 2,
        .pool_capacity  = FRAME_LINK_POOL_SIZE,
        .queue_capacity = FRAME_LINK_QUEUE_SIZE
    };
    if (frame_link_init(&fl_cfg, &srv->frame_link) != 0) {
        LOG_E(MODULE_TAG " 帧链路初始化失败");
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 4. 初始化video数据总线（原有逻辑）
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

    // ====================== 新增：USB摄像头初始化（无侵入） ======================
    srv->cam = camera_usb_create(CAPTURE_DEV_PATH,
                                 srv->width,
                                 srv->height,
                                 srv->v4l2_fmt,
                                 srv->fps);
    if (!srv->cam || camera_init(srv->cam) != 0) {
        LOG_E(MODULE_TAG " USB摄像头初始化失败");
        data_bus_deinit(VIDEO_DATA_BUS_NAME);
        frame_link_deinit(srv->frame_link);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 5. 订阅系统事件（原有逻辑）
    event_subscriber_t evt_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _capture_event_cb,
        .user_data = srv
    };
    srv->evt_sub_id = event_bus_subscribe(SYS_EVENT_BUS_NAME, &evt_sub);
    if (srv->evt_sub_id < 0) {
        LOG_E(MODULE_TAG " 订阅事件总线失败");
        camera_usb_destroy(srv->cam);
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
// 自动初始化：仅初始化，不启动！等待事件触发
// ==========================================================================
#include "initcall.h"
static int _capture_auto_init(void)
{
    // 仅执行初始化，不启动采集
    if (capture_srv_init() != 0) {
        return -1;
    }

    // 按你要求打印日志
    LOG_I(MODULE_TAG " 自动加载完成,等待启动");
    return 0;
}
MODULE_INIT(_capture_auto_init);