/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           capture_srv.c
 * @brief          视频采集服务 - FrameLink 标准合规生产者
 * @author         System Team
 * @date           2025
 * @version        V2.0 适配全新FrameLink终极合规版
 * @constraint     全局唯一生产者 | 绝不阻塞线程 | 零拷贝共享 | 消费者只读
 * @core_flow      摄像头取帧 → FrameLink(仓库)获取空闲帧 → 填充数据 → 推送链路
 *                → DataBus(发票员)广播帧句柄 → 生产者释放自身引用
 ******************************************************************************
 */
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
// 帧链路名称（命名化管理，对标数据总线，文件私有）
#define FRAME_LINK_NAME           "main_cam"         // 采集服务帧链路唯一名称

// 采集核心配置（来源：vision_ai_config.h）
#define CAPTURE_DEV_PATH          CONFIG_CAPTURE_DEV_PATH    // USB摄像头设备节点
#define CAPTURE_WIDTH             CONFIG_CAPTURE_WIDTH       // 固定640
#define CAPTURE_HEIGHT            CONFIG_CAPTURE_HEIGHT      // 固定360
#define CAPTURE_FPS               CONFIG_CAPTURE_FPS         // 固定30
#define CAPTURE_FORMAT_CFG        CONFIG_CAPTURE_FORMAT      // 0=YUYV 1=NV12 2=MJPEG
#define CAPTURE_BUF_CNT           CONFIG_CAPTURE_BUF_COUNT   // 摄像头缓冲区数量
#define MAX_FRAME_SIZE            CAPTURE_WIDTH * CAPTURE_HEIGHT * 2  // 最大帧大小
// 帧链路配置（来源：vision_ai_config.h）
#define FRAME_LINK_POOL_SIZE       CONFIG_FRAME_LINK_POOL_SIZE
#define FRAME_LINK_QUEUE_SIZE      CONFIG_FRAME_LINK_QUEUE_SIZE

// 服务私有固定配置
#define CAP_FRAME_WAIT_US         20000   // 20ms 取帧等待
#define CAP_FPS_INTERVAL_MS       1000    // FPS上报间隔

// ====================== AI模块软件降频配置 ======================
// 硬件30fps，AI目标帧率=5fps（可修改：3/5/10），自动计算降频步长
#define AI_TARGET_FPS             5
#define FPS_DOWNSAMPLE_STEP       (CAPTURE_FPS / AI_TARGET_FPS)  // 30/5=6，每6帧保留1帧给AI

// ==========================================================================
// 采集服务 私有结构体（静态单例，外部完全不可见）
// ==========================================================================
typedef struct {
    camera_base_t          *cam;

    pthread_t               work_thread;
    bool                    thread_running;
    bool                    is_paused;
    bool                    is_started;
    pthread_mutex_t         lock;

    uint32_t                width;
    uint32_t                height;
    uint32_t                fps;
    frame_format_t          frame_fmt;        // 对齐FrameLink标准格式枚举

    uint64_t                frame_count;
    uint64_t                last_fps_ts;

    // 软件降频计数器
    uint32_t                downsample_cnt;

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

// 格式转换：配置 → FrameLink标准格式枚举
static frame_format_t _capture_get_frame_format(int cfg)
{
    switch (cfg) {
        case 0:  return FRAME_FMT_YUYV;
        case 1:  return FRAME_FMT_NV12;
        case 2:  return FRAME_FMT_MJPEG;
        default: return FRAME_FMT_YUYV;
    }
}

// V4L2格式获取（摄像头驱动用）
static uint32_t _capture_get_v4l2_format(frame_format_t fmt)
{
    switch (fmt) {
        case FRAME_FMT_YUYV:  return V4L2_PIX_FMT_YUYV;
        case FRAME_FMT_NV12:  return V4L2_PIX_FMT_NV12;
        case FRAME_FMT_MJPEG: return V4L2_PIX_FMT_MJPEG;
        default:              return V4L2_PIX_FMT_YUYV;
    }
}

// ==========================================================================
// 【核心】服务统一清理函数：安全释放所有资源
// ==========================================================================
static void capture_srv_cleanup(void)
{
    capture_srv_t *srv = &s_capture;

    LOG_W(MODULE_TAG " 开始执行全量资源释放...");

    // 1. 停止工作线程
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

    // 3. 销毁USB摄像头
    if (srv->cam) {
        camera_stop_capture(srv->cam);
        camera_usb_destroy(srv->cam);
        srv->cam = NULL;
        LOG_I(MODULE_TAG " USB摄像头已销毁");
    }

    // 4. 【合规】销毁FrameLink命名链路（仓库销毁）
    frame_link_destroy(FRAME_LINK_NAME);
    LOG_I(MODULE_TAG " FrameLink帧链路已销毁");

    // 5. 销毁数据总线（发票员销毁）
    data_bus_deinit(VIDEO_DATA_BUS_NAME);
    LOG_I(MODULE_TAG " video数据总线已销毁");

    // 6. 销毁线程锁
    pthread_mutex_destroy(&srv->lock);
    LOG_I(MODULE_TAG " 线程锁已销毁");

    // 7. 发布停止事件
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG " 所有资源释放完成，服务已安全退出");
}

// ==========================================================================
// 工作线程：【FrameLink标准生产者】零拷贝 / 无阻塞 / 自动引用计数
// 核心规范（强制遵守）：
// 1. FrameLink(仓库)：唯一管理帧内存、生命周期、引用计数
// 2. DataBus(发票员)：仅转发帧句柄，不管理任何帧资源
// 3. 内存池满直接丢弃帧，绝不阻塞生产线程
// 4. 生产者唯一写权限，推送后变为只读
// ==========================================================================
static void *capture_work_thread(void *arg)
{
    (void)arg;
    capture_srv_t *srv = &s_capture;
    frame_handle_t frame = NULL;
    data_bus_item_handle_t data_item = NULL;
    uint64_t current_ts;
    struct timeval tv;
    void *cam_buf = NULL;
    size_t cam_len = 0;
    const uint32_t frame_data_size = srv->width * srv->height * 2;
    fl_err_t fl_ret;
    void *writable_buf = NULL;
    frame_info_t info = {0};

    LOG_I(MODULE_TAG " 工作线程启动，硬件采集: %ux%u@%uFPS | AI软件降频: %uFPS",
          srv->width, srv->height, srv->fps, AI_TARGET_FPS);

    while (srv->thread_running) {
        // 每次循环重置句柄
        frame = NULL;
        data_item = NULL;
        cam_buf = NULL;

        // 暂停状态处理
        if (srv->is_paused) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        // ============== 【修复1】第一步：先读摄像头原始数据（你的初心） ==============
        if (camera_get_frame(srv->cam, &cam_buf, &cam_len) != 0) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        // ============== 【修复2】第二步：软件降频判断 ==============
        srv->downsample_cnt++;
        bool send_to_bus = (srv->downsample_cnt >= FPS_DOWNSAMPLE_STEP);
        
        // 不达标：直接丢弃摄像头数据，不申请任何帧！
        if (!send_to_bus) {
            srv->frame_count++;
            goto fps_stats; // 直接跳转到FPS统计，不操作帧
        }

        // ============== 【修复3】第三步：降频达标 → 才申请空闲帧 ==============
        srv->downsample_cnt = 0;
        fl_ret = frame_link_producer_get(FRAME_LINK_NAME, &frame);
        if (fl_ret != FL_OK) {
            if (fl_ret == FL_NO_FREE_FRAME) {
                LOG_D(MODULE_TAG " 内存池满，丢弃当前帧");
            }
            // 【调试打印1】获取帧失败
            LOG_D(MODULE_TAG " frame_link_producer_get 失败，ret=%d", fl_ret);
            goto fps_stats;
        }
        // ########################### 调试点1：成功获取帧 ###########################
        LOG_D(MODULE_TAG " ✅ 成功获取空闲帧 | 句柄地址=%p | 链路=%s", frame, FRAME_LINK_NAME);
        // ============== 第四步：填充帧数据 ==============
        writable_buf = frame_get_writable_ptr(frame);
        if (!writable_buf) {
            LOG_E(MODULE_TAG " 获取可写指针失败");
            frame_link_put(frame);  // 🔥 替换新接口
            frame = NULL;
            goto fps_stats;
        }
        // ########################### 调试点2：帧可写指针有效 ###########################
        LOG_D(MODULE_TAG " ✅ 帧可写指针=%p | 摄像头数据地址=%p | 数据长度=%zu", 
              writable_buf, cam_buf, cam_len);
        if (cam_len > frame_data_size) {
            cam_len = frame_data_size;
        }
        memcpy(writable_buf, cam_buf, cam_len);

        // ============== 第五步：填充帧元数据 ==============
        info.width = srv->width;
        info.height = srv->height;
        info.format = srv->frame_fmt;
        info.data_size = cam_len;
        frame_set_info(frame, &info);
        // ============== 第六步：推送链路 + 发布总线 ==============
        fl_ret = frame_link_producer_push(FRAME_LINK_NAME, frame);
        if (fl_ret != FL_OK) {
            LOG_E(MODULE_TAG " FrameLink推送帧失败");
            frame_link_put(frame);  // 🔥 替换新接口
            frame = NULL;
            goto fps_stats;
        }
        // ########################### 调试点3：成功推送帧到链路 ###########################
        LOG_D(MODULE_TAG " ✅ 帧推入FrameLink成功 | ret=%d", fl_ret);
        // DataBus发布
        if (data_bus_alloc(VIDEO_DATA_BUS_NAME,
                           DATA_TYPE_VIDEO_FRAME,
                           sizeof(frame_handle_t),
                           MODULE_NAME,
                           &data_item) == 0)
        {
            frame_handle_t *bus_ptr = (frame_handle_t *)data_bus_get_writable_ptr(data_item);
            *bus_ptr = frame;
            // ########################### 调试点4：数据总线传递句柄 ###########################
            LOG_D(MODULE_TAG " ✅ 数据总线赋值句柄=%p | 总线名称=%s", 
                  *bus_ptr, VIDEO_DATA_BUS_NAME);
            data_bus_publish(VIDEO_DATA_BUS_NAME, data_item);
            event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_FRAME_READY, MODULE_NAME);
        } else {
            LOG_E(MODULE_TAG " ❌ data_bus_alloc 分配失败");
        }

        // ============== 第七步：生产者释放自身引用（唯一一次） ==============
        // ########################### 调试点5：生产者释放帧 ###########################
        LOG_D(MODULE_TAG " 🔄 生产者释放帧 | 帧ID=%u | 句柄=%p", info.frame_id, frame);
        frame_link_put(frame);  // 🔥 替换新接口
        frame = NULL;

        // ============== FPS统计（公共出口） ==============
fps_stats:
        gettimeofday(&tv, NULL);
        current_ts = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        if (current_ts - srv->last_fps_ts >= CAP_FPS_INTERVAL_MS) {
            srv->last_fps_ts = current_ts;
            LOG_D(MODULE_TAG " 采集总FPS: %llu | AI有效FPS: %u", 
                  srv->frame_count, AI_TARGET_FPS);
            srv->frame_count = 0;
        }
    }

    LOG_I(MODULE_TAG " 工作线程退出");
    return NULL;
}

// ==========================================================================
// 服务启动：启动采集线程
// ==========================================================================
static int capture_srv_start(void)
{
    capture_srv_t *srv = &s_capture;

    // 启动摄像头采集
    if (camera_start_capture(srv->cam) != 0) {
        LOG_E(MODULE_TAG " 启动摄像头采集失败");
        return -1;
    }

    // 启动工作线程
    srv->thread_running = true;
    srv->is_paused = false;
    if (pthread_create(&srv->work_thread, NULL, capture_work_thread, NULL) != 0) {
        LOG_E(MODULE_TAG " 创建工作线程失败");
        camera_stop_capture(srv->cam);
        srv->thread_running = false;
        return -1;
    }

    // 发布状态事件
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_READY, MODULE_NAME);
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_RUNNING, MODULE_NAME);

    LOG_I(MODULE_TAG " 服务启动成功，硬件采集运行中");
    return 0;
}

// ==========================================================================
// 事件总线回调：系统事件处理
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
// 服务初始化：FrameLink+DataBus+摄像头 初始化
// ==========================================================================
static int capture_srv_init(void)
{
    capture_srv_t *srv = &s_capture;
    memset(srv, 0, sizeof(capture_srv_t));
    fl_err_t fl_ret;

    // 1. 初始化线程锁
    pthread_mutex_init(&srv->lock, NULL);
    srv->evt_sub_id = -1;
    srv->cam = NULL;
    srv->is_started = false;
    srv->downsample_cnt = 0;

    // 2. 加载硬件配置
    srv->width      = CAPTURE_WIDTH;
    srv->height     = CAPTURE_HEIGHT;
    srv->fps        = CAPTURE_FPS;
    srv->frame_fmt  = _capture_get_frame_format(CAPTURE_FORMAT_CFG);

    // 3. 【仓库】创建FrameLink命名链路
    frame_link_cfg_t fl_cfg = {0};
    strcpy(fl_cfg.name, FRAME_LINK_NAME);  // 直接拷贝，内部自动安全处理
    fl_cfg.max_frame_size = MAX_FRAME_SIZE;
    fl_cfg.pool_count     = FRAME_LINK_POOL_SIZE;
    fl_cfg.queue_count    = FRAME_LINK_QUEUE_SIZE;
    
    fl_ret = frame_link_create(&fl_cfg);
    if (fl_ret != FL_OK) {
        LOG_E(MODULE_TAG " FrameLink创建失败，错误码:%d", fl_ret);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 4. 【发票员】初始化video数据总线
    data_bus_config_t bus_cfg = {0};
    bus_cfg.max_items = 8;
    bus_cfg.max_item_size = sizeof(frame_handle_t);
    bus_cfg.max_subscribers = 8;
    bus_cfg.name = VIDEO_DATA_BUS_NAME;
    
    if (data_bus_init(&bus_cfg) != 0) {
        LOG_E(MODULE_TAG " video数据总线初始化失败");
        frame_link_destroy(FRAME_LINK_NAME);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 5. 初始化USB摄像头
    srv->cam = camera_usb_create(CAPTURE_DEV_PATH,
                                 srv->width,
                                 srv->height,
                                 _capture_get_v4l2_format(srv->frame_fmt),
                                 srv->fps);
    if (!srv->cam || camera_init(srv->cam) != 0) {
        LOG_E(MODULE_TAG " USB摄像头初始化失败");
        data_bus_deinit(VIDEO_DATA_BUS_NAME);
        frame_link_destroy(FRAME_LINK_NAME);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 6. 订阅系统事件
    event_subscriber_t evt_sub = {0};
    evt_sub.event_type = EVENT_TYPE_INVALID;
    evt_sub.callback = _capture_event_cb;
    evt_sub.user_data = srv;
    
    srv->evt_sub_id = event_bus_subscribe(SYS_EVENT_BUS_NAME, &evt_sub);
    if (srv->evt_sub_id < 0) {
        LOG_E(MODULE_TAG " 订阅事件总线失败");
        camera_usb_destroy(srv->cam);
        data_bus_deinit(VIDEO_DATA_BUS_NAME);
        frame_link_destroy(FRAME_LINK_NAME);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    LOG_I(MODULE_TAG " 服务初始化完成 [硬件: %ux%u@%uFPS | AI降频: %uFPS]",
          srv->width, srv->height, srv->fps, AI_TARGET_FPS);
    return 0;
}

// ==========================================================================
// 模块自动初始化
// ==========================================================================
#include "initcall.h"
static int _capture_auto_init(void)
{
    if (capture_srv_init() != 0) {
        return -1;
    }
    LOG_I(MODULE_TAG "_capture_auto_init 自动加载完成,等待系统启动指令");
    return 0;
}
MODULE_INIT_LEVEL(INIT_DEVICE, _capture_auto_init);  