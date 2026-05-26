/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           capture_srv.c
 * @brief          视频采集服务 - DataBus 纯推模式标准生产者
 * @author         System Team
 * @date           2026
 * @version        V3.0 彻底移除FrameLink，纯DataBus架构 | 适配通用线程组件
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
#include "thread.h"   // 新增：引入你封装的通用线程组件
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
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
// MJPEG压缩流时，给128KB安全上限（640*360的JPEG永远超不过）;yuyv原生流时，给宽高*2的上限（YUYV每像素2字节）
#define MAX_FRAME_SIZE            CAPTURE_HEIGHT * CAPTURE_WIDTH * 2               // 最大帧大小(128 * 1024)

// 服务私有固定配置
#define CAP_FRAME_WAIT_US         20000   // 20ms 取帧等待
#define CAP_FPS_INTERVAL_MS       1000    // FPS上报间隔

// ====================== AI模块软件降频配置 ======================
// 硬件30fps，AI目标帧率=5fps（可修改：3/5/10），自动计算降频步长
#define AI_TARGET_FPS             GLOBAL_VIDEO_FPS                
#define FPS_DOWNSAMPLE_STEP       (CAPTURE_FPS / AI_TARGET_FPS)  // 30/5=6，每6帧保留1帧给AI

// 线程配置
#define CAPTURE_THREAD_STACK_SIZE (1024 * 1024)  // 采集线程栈大小 1MB
#define CAPTURE_RT_PRIORITY       80              // 采集实时优先级
#define CAPTURE_CPU_ID            0               // 绑定CPU0（i.MX6ULL单核）

// ==========================================================================
// 采集服务 私有结构体（替换pthread_t为封装的thread_t）
// ==========================================================================
typedef struct {
    // 8字节 指针/句柄
    camera_base_t          *cam;           // 子类基指针
    // 替换：原生pthread → 你封装的通用线程句柄
    thread_t                work_thread;   // 工作线程（封装组件）
    pthread_mutex_t         lock;          // 线程锁
    // 8字节 计数/时间戳
    uint64_t                frame_count;   // 帧总数
    uint64_t                last_fps_ts;   // 上一帧时间戳
    // 4字节 配置/参数
    uint32_t                width;         // 宽度
    uint32_t                height;        // 高度
    uint32_t                fps;           // 帧率
    uint32_t                v4l2_format;   // V4L2摄像头格式
    uint32_t                downsample_cnt;// 降频计数
    int                     evt_sub_id;    // 事件订阅ID

    // 1字节 bool（紧凑放最后）
    bool                    thread_running;// 线程运行
    bool                    is_paused;     // 暂停
    bool                    is_started;    // 已启动
} capture_srv_t;

static capture_srv_t s_capture;

// ==========================================================================
// 内部工具函数
// ==========================================================================
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
// 【核心】服务统一清理函数：适配封装线程组件
// ==========================================================================
static void capture_srv_cleanup(void)
{
    capture_srv_t *srv = &s_capture;

    LOG_W(MODULE_TAG " 开始执行全量资源释放...");

    // 1. 安全停止线程（封装API）
    thread_stop(&srv->work_thread);
    srv->thread_running = false;
    srv->is_paused = true;

    // 替换：原生pthread_join → 封装thread_join
    if (thread_is_running(&srv->work_thread)) {
        thread_join(&srv->work_thread, NULL);
        LOG_I(MODULE_TAG " 工作线程已安全退出");
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
// 工作线程：业务逻辑完全不变，仅修改运行判断
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

    // 替换：用封装线程运行状态判断
    while (thread_is_running(&srv->work_thread)) {
        item = NULL;
        cam_buf = NULL;
        writable_buf = NULL;

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
        
        if (!send_to_bus) {
            srv->frame_count++;
            goto fps_stats;
        }

        // ============== 第三步：降频达标 → 向DataBus申请空闲帧 ==============
        srv->downsample_cnt = 0;
        ret = data_bus_alloc(CAPTURE_DATA_BUS_NAME,
                                 DATA_TYPE_VIDEO,
                                 MAX_FRAME_SIZE,
                                 MODULE_NAME,
                                 &item);
        if (ret != DATA_BUS_OK) {
            if (ret == DATA_BUS_ERR_FULL) {
                LOG_D(MODULE_TAG " 内存池满，丢弃当前帧");
            }
            goto fps_stats;
        }

        // ============== 第四步：填充帧数据 ==============
        writable_buf = data_bus_get_writable_ptr(item);
        if (!writable_buf) {
            LOG_E(MODULE_TAG " 获取可写指针失败");
            data_bus_release(item);
            item = NULL;
            goto fps_stats;
        }
        
        size_t copy_len = utils_min(cam_len, MAX_FRAME_SIZE);
        memcpy(writable_buf, cam_buf, copy_len);

        // ============== 第五步：发布数据到总线 ==============
        ret = data_bus_push(CAPTURE_DATA_BUS_NAME, item);
        if (ret != DATA_BUS_OK) {
            LOG_E(MODULE_TAG " DataBus push发布帧失败，ret=%d", ret);
            data_bus_release(item);
            item = NULL;
            goto fps_stats;
        }

        event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_PROTO_READY, MODULE_NAME);

        // ============== 第六步：生产者释放自身引用 ==============
        data_bus_release(item);
        item = NULL;

        // ============== FPS统计 ==============
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
// 服务启动：【简化版】一键实时线程接口，代码精简70%
// ==========================================================================
static int capture_srv_start(void)
{
    capture_srv_t *srv = &s_capture;
    thread_err_t thread_ret;

    // 启动摄像头采集
    if (camera_start_capture(srv->cam) != 0) {
        LOG_E(MODULE_TAG " 启动摄像头采集失败");
        return -1;
    }

    // 一键创建实时线程：自动完成 命名+栈+优先级+CPU绑定+FIFO调度
    thread_ret = thread_create_rt(&srv->work_thread,
                                  "CAPTURE_Work",
                                  CAPTURE_THREAD_STACK_SIZE,
                                  capture_work_thread,
                                  NULL,
                                  CAPTURE_RT_PRIORITY,
                                  CAPTURE_CPU_ID);

    if (thread_ret != THREAD_OK) {
        LOG_E(MODULE_TAG " 创建实时工作线程失败 err=%d", thread_ret);
        camera_stop_capture(srv->cam);
        return -1;
    }

    // 发布状态事件
    event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_READY, MODULE_NAME);
    event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_RUNNING, MODULE_NAME);

    LOG_I(MODULE_TAG " 服务启动成功，硬件采集运行中 [实时优先级=80 | 绑定CPU0]");
    return 0;
}

// ==========================================================================
// 事件总线回调（完全不变）
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
// 服务初始化（完全不变）
// ==========================================================================
static int capture_srv_init(void)
{
    capture_srv_t *srv = &s_capture;
    memset(srv, 0, sizeof(capture_srv_t));

    pthread_mutex_init(&srv->lock, NULL);
    srv->evt_sub_id = -1;
    srv->cam = NULL;
    srv->is_started = false;
    srv->downsample_cnt = 0;

    srv->width      = CAPTURE_WIDTH;
    srv->height     = CAPTURE_HEIGHT;
    srv->fps        = CAPTURE_FPS;
    srv->v4l2_format = _capture_get_v4l2_format(CAPTURE_FORMAT_CFG);

    data_bus_config_t bus_cfg = {0};
    bus_cfg.max_items = CAPTURE_BUF_CNT;
    bus_cfg.max_item_size = MAX_FRAME_SIZE;
    bus_cfg.max_subscribers = CAPTURE_BUF_CNT;
    bus_cfg.name = CAPTURE_DATA_BUS_NAME;
    
    if (data_bus_init(&bus_cfg) != DATA_BUS_OK) {
        LOG_E(MODULE_TAG " video数据总线(V4.0)初始化失败");
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

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
// 模块自动初始化（完全不变）
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