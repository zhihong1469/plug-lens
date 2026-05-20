/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           net_push_srv.c
 * @brief          网络推流服务模块（RTSP/JPEG + DataBus V4.0 拉模式）
 * @details        1. 订阅AI_RGB总线，事件唤醒无CPU空耗
 *                 2. 纯拉模式获取最新AI处理帧，自动丢弃旧帧
 *                 3. 系统事件控制启停，对齐Demo应用层
 *                 4. TurboJPEG编码 + RTSP推流
 *                 5. 严格遵循DataBus引用计数规范
 * @author         Luo
 * @date           2026
 ******************************************************************************
 */

// ==========================================================================
// 【文件私有化宏定义】
// ==========================================================================
#define MODULE_NAME               "NET_PUSH"
#define MODULE_TAG                "[NET_PUSH]"

/* 数据总线/事件总线 （全局配置统一定义） */
#define AI_RGB_DATA_BUS           AI_RGB_DATA_BUS_NAME
#define SYS_EVENT_BUS             SYS_EVENT_BUS_NAME

/* 推流参数配置 */
#define FRAME_WAIT_TIMEOUT_MS     30
#define JPEG_DEFAULT_QUALITY      80

/* 视频参数（匹配AI输出 640*360 RGB24） */
#define VIDEO_WIDTH               CONFIG_CAPTURE_WIDTH
#define VIDEO_HEIGHT              CONFIG_CAPTURE_HEIGHT
#define VIDEO_STRIDE              (VIDEO_WIDTH * 3)
#define RGB_FRAME_SIZE            (VIDEO_WIDTH * VIDEO_HEIGHT * 3)

// ==========================================================================
// 头文件包含
// ==========================================================================
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "vision_ai_config.h"
#include "initcall.h"

// 第三方依赖
#include <turbojpeg.h>
#include "rtsp_server.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

/* =============================================================================
 * @brief 网络推流服务控制块（对齐人脸服务结构）
 * ============================================================================*/
typedef struct {
    // 编码/推流句柄
    tjhandle               tj_handle;     // TurboJPEG编码器句柄
    int                    jpeg_quality;  // JPEG编码质量

    /* 线程控制 */
    pthread_t               work_thread;           /* 工作线程 */
    pthread_mutex_t         mutex;                 /* 条件变量互斥锁 */
    pthread_cond_t          cond;                  /* 事件唤醒条件变量 */
    bool                    thread_running;        /* 线程运行标志 */
    bool                    is_paused;             /* 服务暂停标志 */
    bool                    is_started;            /* 服务启动标志 */

    /* 事件订阅ID */
    int                     evt_sys_sub_id;        /* 系统事件订阅ID */
    int                     evt_ai_sub_id;         /* AI处理完成事件订阅ID */
} net_push_srv_t;

/* 全局单例 */
static net_push_srv_t s_net_push_srv;

/* =============================================================================
 * 静态函数声明
 * ============================================================================*/
static void  net_push_event_cb(const event_t *event, void *user_data);
static void *net_push_work_thread(void *arg);
static int   net_push_srv_start(void);
static void  net_push_srv_cleanup(void);
static int   net_push_srv_init(void);
static int   net_push_srv_auto_init(void);

/* =============================================================================
 * @brief 毫秒级条件等待（通用工具函数）
 * ============================================================================*/
static int pthread_cond_timedwait_ms(pthread_cond_t *cond,
                                     pthread_mutex_t *mutex,
                                     uint32_t timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000UL;

    if (ts.tv_nsec >= 1000000000UL) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000UL;
    }

    return pthread_cond_timedwait(cond, mutex, &ts);
}

/* =============================================================================
 * @brief   事件总线回调（系统事件 + AI帧处理完成事件）
 * ============================================================================*/
static void net_push_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    net_push_srv_t *srv = &s_net_push_srv;

    switch (event->type)
    {
        /* AI处理完成：唤醒推流线程处理帧 */
        case EVENT_TYPE_FACE_PROCESS_DONE:
            if (srv->thread_running && !srv->is_paused)
            {
                pthread_mutex_lock(&srv->mutex);
                pthread_cond_signal(&srv->cond);
                pthread_mutex_unlock(&srv->mutex);
            }
            break;

        /* 系统恢复：启动推流服务 */
        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started)
            {
                net_push_srv_start();
                srv->is_started = true;
            }
            else
            {
                srv->is_paused = false;
                LOG_I(MODULE_TAG "服务恢复运行");
            }
            break;

        /* 系统暂停 */
        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG "服务进入暂停状态");
            srv->is_paused = true;
            break;

        /* 系统停止/关机 */
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
        case EVENT_TYPE_SYS_ERROR:
            net_push_srv_cleanup();
            break;

        default:
            break;
    }
}

/* =============================================================================
 * @brief   推流工作线程（拉模式 + 事件唤醒）
 * ============================================================================*/
static void *net_push_work_thread(void *arg)
{
    (void)arg;
    net_push_srv_t *srv = &s_net_push_srv;
    data_bus_item_handle_t rgb_item = NULL;
    int ret;

    LOG_I(MODULE_TAG "推流工作线程启动【拉模式+事件唤醒】");

    while (srv->thread_running)
    {
        /* 暂停状态：低功耗等待 */
        if (srv->is_paused)
        {
            usleep(FRAME_WAIT_TIMEOUT_MS * 1000);
            continue;
        }

        /* 等待AI事件唤醒 */
        pthread_mutex_lock(&srv->mutex);
        pthread_cond_timedwait_ms(&srv->cond, &srv->mutex, FRAME_WAIT_TIMEOUT_MS);
        pthread_mutex_unlock(&srv->mutex);

        /* 拉取最新AI RGB帧 */
        ret = data_bus_pull_latest(AI_RGB_DATA_BUS, DATA_TYPE_VIDEO_RGB, &rgb_item);
        if (ret != DATA_BUS_OK || !rgb_item)
        {
            continue;
        }

        /* 获取RGB数据指针 */
        const uint8_t *rgb_data = data_bus_get_readonly_ptr(rgb_item);
        if (!rgb_data)
        {
            data_bus_release(rgb_item);
            rgb_item = NULL;
            continue;
        }

        /* JPEG编码 */
        uint8_t *jpeg_buf = NULL;
        unsigned long jpeg_size = 0;

        ret = tjCompress2(srv->tj_handle,
                          (unsigned char *)rgb_data,
                          VIDEO_WIDTH,
                          VIDEO_STRIDE,
                          VIDEO_HEIGHT,
                          TJPF_RGB,
                          &jpeg_buf,
                          &jpeg_size,
                          TJSAMP_420,
                          srv->jpeg_quality,
                          TJFLAG_FASTDCT);

        /* 推流成功 */
        if (ret == 0 && jpeg_buf && jpeg_size > 0)
        {
            rtsp_server_push_jpeg(jpeg_buf, jpeg_size);
            LOG_D(MODULE_TAG "推流成功 | 大小:%lu Bytes", jpeg_size);
        }
        else
        {
            LOG_E(MODULE_TAG "JPEG编码失败");
        }

        /* 资源释放（严格配对） */
        if (jpeg_buf) tjFree(jpeg_buf);
        data_bus_release(rgb_item);
        rgb_item = NULL;
    }

    LOG_I(MODULE_TAG "推流工作线程安全退出");
    return NULL;
}

/* =============================================================================
 * @brief   服务启动函数（仅事件触发调用）
 * ============================================================================*/
static int net_push_srv_start(void)
{
    net_push_srv_t *srv = &s_net_push_srv;
    int ret = -1;

    /* 初始化条件变量 */
    ret = pthread_cond_init(&srv->cond, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "条件变量初始化失败");
        return -1;
    }

    /* 启动RTSP服务 */
    ret = rtsp_server_start();
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "RTSP服务启动失败");
        pthread_cond_destroy(&srv->cond);
        return -2;
    }

    /* 创建工作线程 */
    srv->thread_running = true;
    srv->is_paused = false;
    ret = pthread_create(&srv->work_thread, NULL, net_push_work_thread, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "线程创建失败");
        rtsp_server_stop();
        pthread_cond_destroy(&srv->cond);
        srv->thread_running = false;
        return -3;
    }

    /* 发布服务就绪事件 */
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_NET_READY, MODULE_NAME);
    LOG_I(MODULE_TAG "网络推流服务启动成功");
    return 0;
}

/* =============================================================================
 * @brief   服务资源清理（安全退出）
 * ============================================================================*/
static void net_push_srv_cleanup(void)
{
    net_push_srv_t *srv = &s_net_push_srv;

    LOG_W(MODULE_TAG "开始释放所有资源");

    /* 停止线程 */
    srv->thread_running = false;
    srv->is_paused = true;

    /* 唤醒线程退出 */
    pthread_mutex_lock(&srv->mutex);
    pthread_cond_signal(&srv->cond);
    pthread_mutex_unlock(&srv->mutex);

    /* 等待线程退出 */
    if (srv->work_thread > 0)
    {
        pthread_join(srv->work_thread, NULL);
    }

    /* 取消事件订阅 */
    if (srv->evt_sys_sub_id >= 0)
    {
        event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_sys_sub_id);
    }
    if (srv->evt_ai_sub_id >= 0)
    {
        event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_ai_sub_id);
    }

    /* 释放编码器 */
    if (srv->tj_handle)
    {
        tjDestroy(srv->tj_handle);
        srv->tj_handle = NULL;
    }

    /* 停止RTSP */
    rtsp_server_stop();

    /* 销毁同步对象 */
    pthread_cond_destroy(&srv->cond);
    pthread_mutex_destroy(&srv->mutex);

    /* 发布停止事件 */
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_NET_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG "所有资源释放完成");
}

/* =============================================================================
 * @brief   服务初始化（仅准备资源，无线程/阻塞操作）
 * ============================================================================*/
static int net_push_srv_init(void)
{
    net_push_srv_t *srv = &s_net_push_srv;
    int ret = -1;

    /* 清空控制块 */
    memset(srv, 0, sizeof(net_push_srv_t));
    srv->evt_sys_sub_id = -1;
    srv->evt_ai_sub_id = -1;
    srv->jpeg_quality = JPEG_DEFAULT_QUALITY;

    /* 初始化互斥锁 */
    ret = pthread_mutex_init(&srv->mutex, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "互斥锁初始化失败");
        return -1;
    }

    /* 初始化TurboJPEG编码器 */
    srv->tj_handle = tjInitCompress();
    if (!srv->tj_handle)
    {
        LOG_E(MODULE_TAG "TurboJPEG初始化失败");
        pthread_mutex_destroy(&srv->mutex);
        return -2;
    }

    /* 订阅系统事件 */
    event_subscriber_t sys_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = net_push_event_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_sys_sub_id = event_bus_subscribe(SYS_EVENT_BUS, &sys_sub);

    /* 订阅AI处理完成事件（核心唤醒源） */
    event_subscriber_t ai_sub = {
        .event_type = EVENT_TYPE_FACE_PROCESS_DONE,
        .callback = net_push_event_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_ai_sub_id = event_bus_subscribe(SYS_EVENT_BUS, &ai_sub);

    /* 订阅检查 */
    if (srv->evt_sys_sub_id < 0 || srv->evt_ai_sub_id < 0)
    {
        LOG_E(MODULE_TAG "事件订阅失败");
        net_push_srv_cleanup();
        return -3;
    }

    LOG_I(MODULE_TAG "网络推流服务初始化完成");
    return 0;
}

/* =============================================================================
 * @brief   模块自动初始化（系统启动自动加载）
 * ============================================================================*/
static int net_push_srv_auto_init(void)
{
    if (net_push_srv_init() != 0)
    {
        return -1;
    }
    LOG_I(MODULE_TAG "模块自动加载完成，等待系统启动指令");
    return 0;
}

// 注册服务初始化级别
MODULE_INIT_LEVEL(INIT_SERVICE, net_push_srv_auto_init);

/******************************* End of file **********************************/