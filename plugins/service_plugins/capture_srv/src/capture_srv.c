/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           capture_srv.c
 * @brief          视频采集服务 - DataBus 纯推模式标准生产者
 * @author         System Team
 * @date           2026
 * @version        V3.0 彻底移除FrameLink，纯DataBus架构
 * @constraint     全局唯一生产者 | 绝不阻塞线程 | 零拷贝共享 | 消费者只读
 * @core_flow      摄像头取帧 → DataBus申请空闲内存 → 填充数据 → 发布总线
 *                → 自动通知所有订阅者 → 生产者释放自身引用
 ******************************************************************************
 */
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "utils.h"
#include "vision_ai_config.h"
#include "camera_usb.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
// ==========================================================================
// 全局宏定义（文件私有化，标注来源，方便代码巡查）
// 来源：common\configs\vision_ai_config.h
// ==========================================================================
#define MODULE_NAME           "CAPTURE"
#define MODULE_TAG            "[CAPTURE]"

// 系统总线名称（全局约定）
#define CAPTURE_EVENT_BUS_NAME        SYS_EVENT_BUS_NAME        // 来源：vision_ai_config.h
#define CAPTURE_DATA_BUS_NAME         VIDEO_DATA_BUS_NAME            // 视频数据总线唯一名称

// 采集核心配置（来源：vision_ai_config.h）
#define CAPTURE_DEV_PATH          CONFIG_CAPTURE_DEV_PATH    // USB摄像头设备节点
#define CAPTURE_WIDTH             GLOBAL_VIDEO_WIDTH       // 固定640
#define CAPTURE_HEIGHT            GLOBAL_VIDEO_HEIGHT      // 固定360
#define CAPTURE_FPS               CONFIG_CAPTURE_FPS         // 固定30
#define CAPTURE_FORMAT_CFG        CONFIG_CAPTURE_FORMAT      // 0=YUYV 1=NV12 2=MJPEG
#define CAPTURE_BUF_CNT           CONFIG_CAPTURE_BUF_COUNT   // 摄像头缓冲区数量
// MJPEG压缩流，给128KB安全上限（640*360的JPEG永远超不过）
#define MAX_FRAME_SIZE            (128 * 1024)               // 最大帧大小

// 服务私有固定配置
#define CAP_FRAME_WAIT_US         20000   // 20ms 取帧等待
#define CAP_FPS_INTERVAL_MS       1000    // FPS上报间隔

// ====================== AI模块软件降频配置 ======================
// 硬件30fps，AI目标帧率=5fps（可修改：3/5/10），自动计算降频步长
#define AI_TARGET_FPS             GLOBAL_VIDEO_FPS                
#define FPS_DOWNSAMPLE_STEP       (CAPTURE_FPS / AI_TARGET_FPS)  // 30/5=6，每6帧保留1帧给AI

// ==========================================================================
// 采集服务 私有结构体（静态单例，外部完全不可见）
// ==========================================================================
typedef struct {
    // 8字节 指针/句柄
    camera_base_t          *cam;           // 子类基指针 8/4B
    // 8字节 线程/锁
    pthread_t               work_thread;   // 工作线程 8B
    pthread_mutex_t         lock;          // 线程锁 8/4B
    // 8字节 计数/时间戳
    uint64_t                frame_count;   // 帧总数 8B
    uint64_t                last_fps_ts;   // 上一帧时间戳 8B
    // 4字节 配置/参数
    uint32_t                width;         // 宽度 4B
    uint32_t                height;        // 高度 4B
    uint32_t                fps;           // 帧率 4B
    uint32_t                v4l2_format;   // V4L2摄像头格式 4B
    uint32_t                downsample_cnt;// 降频计数 4B
    int                     evt_sub_id;    // 事件订阅ID 4B

    // 1字节 bool（紧凑放最后）
    bool                    thread_running;// 线程运行 1B
    bool                    is_paused;     // 暂停 1B
    bool                    is_started;    // 已启动 1B
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

// 格式转换：配置 → V4L2标准格式枚举
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
        event_bus_unsubscribe(CAPTURE_EVENT_BUS_NAME, srv->evt_sub_id);
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

    // 4. 销毁视频数据总线
    data_bus_deinit(CAPTURE_DATA_BUS_NAME);
    LOG_I(MODULE_TAG " video数据总线已销毁");

    // 5. 销毁线程锁
    pthread_mutex_destroy(&srv->lock);
    LOG_I(MODULE_TAG " 线程锁已销毁");

    // 6. 发布停止事件
    event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG " 所有资源释放完成，服务已安全退出");
}

// ==========================================================================
// 工作线程：【DataBus V4.0标准生产者】零拷贝 / 无阻塞 / 自动引用计数
// 核心规范（强制遵守）：
// 1. DataBus V4.0：唯一管理帧内存、生命周期、引用计数
// 2. 内存池满直接丢弃帧，绝不阻塞生产线程
// 3. 生产者唯一写权限，push发布后变为只读
// 4. 推模式自动通知所有订阅者
// 5. 生产者必须在push后释放自身引用
// ==========================================================================
static void *capture_work_thread(void *arg)
{
    (void)arg;
    capture_srv_t *srv = &s_capture;
    data_bus_item_handle_t item = NULL;
    uint64_t current_ts;
    struct timeval tv;
    void *cam_buf = NULL;
    size_t cam_len = 0;
    void *writable_buf = NULL;
    int ret = 0;

    LOG_I(MODULE_TAG " 工作线程启动，硬件采集: %ux%u@%uFPS | AI软件降频: %uFPS",
          srv->width, srv->height, srv->fps, AI_TARGET_FPS);

    while (srv->thread_running) {
        // 每次循环重置句柄
        item = NULL;
        cam_buf = NULL;
        writable_buf = NULL;

        // 暂停状态处理
        if (srv->is_paused) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        // ============== 第一步：先读摄像头原始数据 ==============
        if (camera_get_frame(srv->cam, &cam_buf, &cam_len) != 0) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        // ============== 第二步：软件降频判断 ==============
        srv->downsample_cnt++;
        bool send_to_bus = (srv->downsample_cnt >= FPS_DOWNSAMPLE_STEP);
        
        // 不达标：直接丢弃摄像头数据，不申请任何内存！
        if (!send_to_bus) {
            srv->frame_count++;
            goto fps_stats; // 直接跳转到FPS统计，不操作总线
        }

        // ============== 第三步：降频达标 → 向DataBus V4.0申请空闲帧 ==============
        srv->downsample_cnt = 0;
        ret = data_bus_alloc(CAPTURE_DATA_BUS_NAME,
                                 DATA_TYPE_VIDEO,
                                 MAX_FRAME_SIZE,
                                 MODULE_NAME,
                                 &item);
        if (ret != DATA_BUS_OK) {
            if (ret == DATA_BUS_ERR_FULL) { // 内存池满
                LOG_D(MODULE_TAG " 内存池满，丢弃当前帧");
            } else {
                LOG_D(MODULE_TAG " data_bus_alloc 失败，ret=%d", ret);
            }
            goto fps_stats;
        }
        LOG_D(MODULE_TAG " ✅ 成功获取空闲帧 | 句柄地址=%p | 总线=%s", item, CAPTURE_DATA_BUS_NAME);

        // ============== 第四步：填充帧数据 ==============
        writable_buf = data_bus_get_writable_ptr(item);
        if (!writable_buf) {
            LOG_E(MODULE_TAG " 获取可写指针失败");
            data_bus_release(item); // FIX: 异常必须释放
            item = NULL;
            goto fps_stats;
        }
        LOG_D(MODULE_TAG " ✅ 帧可写指针=%p | 摄像头数据地址=%p | 数据长度=%zu", 
              writable_buf, cam_buf, cam_len);
        
        // 安全拷贝，防止越界
        size_t copy_len = utils_min(cam_len, MAX_FRAME_SIZE);
        memcpy(writable_buf, cam_buf, copy_len);

        // ============== 第五步：V4.0正式API发布数据到总线 ==============
        ret = data_bus_push(CAPTURE_DATA_BUS_NAME, item); // FIX: 替换旧版publish为正式push
        if (ret != DATA_BUS_OK) {
            LOG_E(MODULE_TAG " DataBus push发布帧失败，ret=%d", ret);
            data_bus_release(item); // FIX: 发布失败必须释放
            item = NULL;
            goto fps_stats;
        }
        LOG_D(MODULE_TAG " ✅ 帧推入DataBus成功 | ret=%d", ret);

        // 发布事件通知（轻量唤醒，不传递数据）
        event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_PROTO_READY, MODULE_NAME);

        // ============== 第六步：生产者释放自身引用（V4.0强制规范） ==============
        LOG_D(MODULE_TAG " 🔄 生产者释放帧 | 句柄=%p", item);
        data_bus_release(item);
        item = NULL;

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
    int ret = -1;
    pthread_attr_t thread_attr;
    struct sched_param sched_param;

    // 启动摄像头采集
    if (camera_start_capture(srv->cam) != 0) {
        LOG_E(MODULE_TAG " 启动摄像头采集失败");
        return -1;
    }

    // 初始化线程属性 + 设置实时优先级（核心：采集优先级80）
    pthread_attr_init(&thread_attr);
    pthread_attr_setschedpolicy(&thread_attr, SCHED_FIFO);
    sched_param.sched_priority = 80;
    pthread_attr_setschedparam(&thread_attr, &sched_param);
    pthread_attr_setinheritsched(&thread_attr, PTHREAD_EXPLICIT_SCHED);

    // 启动工作线程
    srv->thread_running = true;
    srv->is_paused = false;
    ret = pthread_create(&srv->work_thread, &thread_attr, capture_work_thread, NULL);
    if (ret != 0) {
        LOG_E(MODULE_TAG " 创建工作线程失败 err=%d", ret);
        pthread_attr_destroy(&thread_attr);
        camera_stop_capture(srv->cam);
        srv->thread_running = false;
        return -1;
    }

    // 销毁线程属性
    pthread_attr_destroy(&thread_attr);

    // 发布状态事件
    event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_READY, MODULE_NAME);
    event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_RUNNING, MODULE_NAME);

    LOG_I(MODULE_TAG " 服务启动成功，硬件采集运行中 [优先级=80]");
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
// 服务初始化：DataBus+摄像头 初始化
// ==========================================================================
static int capture_srv_init(void)
{
    capture_srv_t *srv = &s_capture;
    memset(srv, 0, sizeof(capture_srv_t));

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
    srv->v4l2_format = _capture_get_v4l2_format(CAPTURE_FORMAT_CFG);

    // 3. 初始化视频数据总线（V4.0 标准配置）
    data_bus_config_t bus_cfg = {0};
    bus_cfg.max_items = CAPTURE_BUF_CNT;                // 帧缓存数量
    bus_cfg.max_item_size = MAX_FRAME_SIZE; // 单帧最大大小
    bus_cfg.max_subscribers = CAPTURE_BUF_CNT;          // 最大订阅者数量
    bus_cfg.name = CAPTURE_DATA_BUS_NAME;
    
    if (data_bus_init(&bus_cfg) != DATA_BUS_OK) {
        LOG_E(MODULE_TAG " video数据总线(V4.0)初始化失败");
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 4. 初始化USB摄像头
    srv->cam = camera_usb_create(CAPTURE_DEV_PATH,
                                 srv->width,
                                 srv->height,
                                 srv->v4l2_format,
                                 srv->fps);
    if (!srv->cam || camera_init(srv->cam) != 0) {
        LOG_E(MODULE_TAG " USB摄像头初始化失败");
        data_bus_deinit(CAPTURE_DATA_BUS_NAME);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 5. 订阅系统事件
    event_subscriber_t evt_sub = {0};
    evt_sub.event_type = EVENT_TYPE_INVALID;
    evt_sub.callback = _capture_event_cb;
    evt_sub.user_data = srv;
    
    srv->evt_sub_id = event_bus_subscribe(CAPTURE_EVENT_BUS_NAME, &evt_sub);
    if (srv->evt_sub_id < 0) {
        LOG_E(MODULE_TAG " 订阅事件总线失败");
        camera_usb_destroy(srv->cam);
        data_bus_deinit(CAPTURE_DATA_BUS_NAME);
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