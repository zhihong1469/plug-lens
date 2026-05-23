/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           net_push_srv.c
 * @brief          网络推流服务模块（RTSP H.264 + DataBus V4.0 优先级拉模式）
 * @details        1. 【优先级拉流】优先订阅人脸带框帧，降级原始摄像头总线
 *                 2. 事件唤醒无CPU空耗，自动丢弃旧帧
 *                 3. 系统事件控制启停，对齐全应用层架构
 *                 4. 【优化】YUYV原始帧转H.264编码推流，标准RTSP兼容
 *                 5. 严格遵循DataBus引用计数规范
 *                 6. 基于img_joint H.264编码器，IMX6ULL高性能适配
 * @author         Luo
 * @date           2026
 ******************************************************************************
 */

// ==========================================================================
// 头文件包含
// ==========================================================================
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "vision_ai_config.h"
#include "initcall.h"
#include "img_joint.h"  // 新增：H.264编码器+图像转换

// 第三方依赖
#include "rtsp_server.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

// ==========================================================================
// 【文件内部私有化宏】可直接在此调整推流参数，无需修改函数逻辑
// ==========================================================================
#define MODULE_NAME               "NET_PUSH"
#define MODULE_TAG                "[NET_PUSH]"

#define NET_PUSH_TARGET_FPS        GLOBAL_VIDEO_FPS          // 推流帧率
#define FRAME_INTERVAL_MS          GLOBAL_FRAME_INTERVAL_MS  // 自动计算帧间隔

/* 数据总线配置（优先级：人脸带框RGB帧 > 原始摄像头MJPEG/YUYV帧） */
#define FACE_RESULT_RGB_DATA_BUS  FACE_YUV_DATA_BUS_NAME   // 高优先级：带人脸框的RGB帧
#define VIDEO_DATA_BUS            VIDEO_DATA_BUS_NAME      // 低优先级：摄像头原生YUYV帧
#define SYS_EVENT_BUS             SYS_EVENT_BUS_NAME

/* 推流参数配置 */
#define FRAME_WAIT_TIMEOUT_MS     30

/* 视频参数（匹配摄像头原生MJPEG/YUYV格式） */
#define VIDEO_WIDTH               GLOBAL_VIDEO_WIDTH
#define VIDEO_HEIGHT              GLOBAL_VIDEO_HEIGHT
#define H264_BITRATE              500     // H.264码率(kbps) IMX6ULL推荐300~800
#define H264_GOP                  15      // I帧间隔

/* H.264输出缓冲区大小（足够容纳编码后数据） */
#define H264_BUF_SIZE             (VIDEO_WIDTH * VIDEO_HEIGHT)

// ==========================================================================
// @brief 网络推流服务控制块（新增H.264编码器成员）
// ============================================================================
typedef struct {
    /* 线程控制 */
    pthread_t               work_thread;
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;
    bool                    thread_running;
    bool                    is_paused;
    bool                    is_started;

    /* 事件订阅ID */
    int                     evt_sys_sub_id;
    int                     evt_ai_sub_id;

    /* H.264编码器相关 */
    h264_encoder_t          h264_enc;        // H.264编码器句柄
    uint8_t*                h264_buf;        // H.264编码输出缓冲区
} net_push_srv_t;

/* 全局单例 */
static net_push_srv_t s_net_push_srv;

// ==========================================================================
// 静态函数声明
// ============================================================================
static void  net_push_event_cb(const event_t *event, void *user_data);
static void *net_push_work_thread(void *arg);
static int   net_push_srv_start(void);
static void  net_push_srv_cleanup(void);
static int   net_push_srv_init(void);
static int   net_push_srv_auto_init(void);

// ==========================================================================
// @brief 毫秒级条件等待（通用工具函数）
// ============================================================================
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

// ==========================================================================
// @brief   事件总线回调（系统事件 + AI处理完成事件）
// ============================================================================
static void net_push_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    net_push_srv_t *srv = &s_net_push_srv;

    switch (event->type)
    {
        /* AI处理完成：唤醒推流线程（优先级拉流触发） */
        case EVENT_TYPE_FACE_PROCESS_DONE:
            if (srv->thread_running && !srv->is_paused)
            {
                pthread_mutex_lock(&srv->mutex);
                pthread_cond_signal(&srv->cond);
                pthread_mutex_unlock(&srv->mutex);
                LOG_D(MODULE_TAG "收到AI完成事件，唤醒推流线程");
            }
            break;

        /* 系统恢复：启动推流服务 */
        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started)
            {
                net_push_srv_start();
                srv->is_started = true;
                LOG_I(MODULE_TAG "系统RESUME，启动推流服务");
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

// ==========================================================================
// @brief   推流工作线程（YUYV → H.264编码 → RTSP推流）
// @details 1. 拉取摄像头原始YUYV帧
//          2. img_joint编码为H.264
//          3. RTSP推送H.264码流
//          4. 帧率限流+事件唤醒，低CPU占用
// ============================================================================
static void *net_push_work_thread(void *arg)
{
    net_push_srv_t *srv = &s_net_push_srv;
    data_bus_item_handle_t frame_item = NULL;
    const uint8_t *frame_data = NULL;
    size_t frame_size = 0;
    int h264_len = 0;
    struct timespec last_ts;

    clock_gettime(CLOCK_MONOTONIC, &last_ts);
    // 新增：线程启动日志
    LOG_I(MODULE_TAG "推流工作线程启动成功，等待数据...");

    while (srv->thread_running)
    {
        if (srv->is_paused) {
            usleep(FRAME_INTERVAL_MS * 1000);
            continue;
        }

        // 等待事件唤醒
        pthread_mutex_lock(&srv->mutex);
        pthread_cond_timedwait_ms(&srv->cond, &srv->mutex, FRAME_INTERVAL_MS);
        pthread_mutex_unlock(&srv->mutex);

        // 帧率限流
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint32_t elapsed = (now.tv_sec - last_ts.tv_sec)*1000 + (now.tv_nsec - last_ts.tv_nsec)/1000000;
        if (elapsed < FRAME_INTERVAL_MS) continue;
        last_ts = now;

        // ===================== 核心：拉取YUYV + H.264编码 + 推流 =====================
        frame_item = NULL;
        if (data_bus_pull_latest(VIDEO_DATA_BUS, DATA_TYPE_VIDEO, &frame_item) == DATA_BUS_OK) {
            frame_data = data_bus_get_readonly_ptr(frame_item);
            frame_size = data_bus_get_item_size(frame_item);
            // 新增：拉取数据成功日志
            LOG_D(MODULE_TAG "拉取YUYV帧成功 | 大小：%zu", frame_size);

            // 编码YUYV为H.264
            if (frame_data && frame_size && srv->h264_enc && srv->h264_buf) {
                h264_len = H264_BUF_SIZE;
                // 新增：编码开始日志
                LOG_D(MODULE_TAG "开始YUYV转H264编码...");
                if (yuyv_to_h264(srv->h264_enc, frame_data, frame_size, srv->h264_buf, &h264_len) == IMG_JOINT_OK) {
                    // 新增：编码成功+推流成功日志
                    LOG_D(MODULE_TAG "H264编码成功 | 输出大小：%d", h264_len);
                    // 推送H.264码流
                    rtsp_server_push_h264(srv->h264_buf, h264_len);
                    LOG_D(MODULE_TAG "RTSP推送H264帧完成");
                } else {
                    // 新增：编码失败日志
                    LOG_E(MODULE_TAG "YUYV转H264编码失败！");
                }
            } else {
                // 新增：参数无效日志
                LOG_E(MODULE_TAG "编码参数无效，跳过编码！");
            }
            // 释放总线数据
            data_bus_release(frame_item);
            LOG_D(MODULE_TAG "释放DataBus数据帧");
        } else {
            // 新增：拉取数据失败日志
            LOG_W(MODULE_TAG "拉取VIDEO_DATA_BUS数据失败");
        }
    }

    // 新增：线程退出日志
    LOG_I(MODULE_TAG "推流工作线程正常退出");
    return NULL;
}

// ==========================================================================
// @brief   服务启动函数（初始化H.264编码器）
// ============================================================================
static int net_push_srv_start(void)
{
    net_push_srv_t *srv = &s_net_push_srv;
    int ret = -1;
    pthread_attr_t thread_attr;
    struct sched_param sched_param;

    /* 初始化条件变量 */
    ret = pthread_cond_init(&srv->cond, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "条件变量初始化失败");
        return -1;
    }

    /* ===================== H.264编码器初始化 ===================== */
    h264_encode_param_t enc_param = {
        .width = VIDEO_WIDTH,
        .height = VIDEO_HEIGHT,
        .fps = NET_PUSH_TARGET_FPS,
        .bitrate = H264_BITRATE,
        .gop = H264_GOP,
        .use_cpu_core = true
    };
    LOG_I(MODULE_TAG "创建H264编码器 | 分辨率：%dx%d | FPS：%d | 码率：%dkbps",
          VIDEO_WIDTH, VIDEO_HEIGHT, NET_PUSH_TARGET_FPS, H264_BITRATE);
    srv->h264_enc = h264_encoder_create(&enc_param);
    if (!srv->h264_enc) {
        LOG_E(MODULE_TAG "H.264编码器创建失败");
        pthread_cond_destroy(&srv->cond);
        return -2;
    }
    LOG_I(MODULE_TAG "H264编码器创建成功");

    /* 分配H.264编码缓冲区 */
    srv->h264_buf = (uint8_t*)malloc(H264_BUF_SIZE);
    if (!srv->h264_buf) {
        LOG_E(MODULE_TAG "H.264缓冲区分配失败");
        h264_encoder_destroy(srv->h264_enc);
        pthread_cond_destroy(&srv->cond);
        return -3;
    }
    LOG_D(MODULE_TAG "H264缓冲区分配成功 | 大小：%d", H264_BUF_SIZE);

    /* 启动RTSP服务（H.264模式） */
    ret = rtsp_server_start();
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "RTSP服务启动失败");
        free(srv->h264_buf);
        h264_encoder_destroy(srv->h264_enc);
        pthread_cond_destroy(&srv->cond);
        return -4;
    }
    LOG_I(MODULE_TAG "RTSP服务启动成功");

    /* 初始化线程属性 + 设置实时优先级 */
    pthread_attr_init(&thread_attr);
    pthread_attr_setschedpolicy(&thread_attr, SCHED_FIFO);
    sched_param.sched_priority = 90;
    pthread_attr_setschedparam(&thread_attr, &sched_param);
    pthread_attr_setinheritsched(&thread_attr, PTHREAD_EXPLICIT_SCHED);

    /* 创建工作线程 */
    srv->thread_running = true;
    srv->is_paused = false;
    ret = pthread_create(&srv->work_thread, &thread_attr, net_push_work_thread, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "线程创建失败 err=%d", ret);
        pthread_attr_destroy(&thread_attr);
        free(srv->h264_buf);
        h264_encoder_destroy(srv->h264_enc);
        rtsp_server_stop();
        pthread_cond_destroy(&srv->cond);
        srv->thread_running = false;
        return -5;
    }

    pthread_attr_destroy(&thread_attr);

    /* 发布服务就绪事件 */
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_NET_READY, MODULE_NAME);
    LOG_I(MODULE_TAG "网络推流服务启动成功（H.264编码）[优先级=90] [分辨率=%dx%d] [FPS=%d]",
          VIDEO_WIDTH, VIDEO_HEIGHT, NET_PUSH_TARGET_FPS);
    return 0;
}

// ==========================================================================
// @brief   服务资源清理（释放H.264编码器）
// ============================================================================
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
        LOG_I(MODULE_TAG "推流线程已退出");
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

    /* ===================== 释放H.264资源 ===================== */
    if (srv->h264_buf) {
        free(srv->h264_buf);
        srv->h264_buf = NULL;
        LOG_D(MODULE_TAG "释放H264缓冲区");
    }
    if (srv->h264_enc) {
        h264_encoder_destroy(srv->h264_enc);
        srv->h264_enc = NULL;
        LOG_I(MODULE_TAG "销毁H264编码器");
    }

    /* 停止RTSP */
    rtsp_server_stop();
    LOG_I(MODULE_TAG "RTSP服务已停止");

    /* 销毁同步对象 */
    pthread_cond_destroy(&srv->cond);
    pthread_mutex_destroy(&srv->mutex);

    /* 发布停止事件 */
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_NET_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG "所有资源释放完成");
}

// ==========================================================================
// @brief   服务初始化
// ============================================================================
static int net_push_srv_init(void)
{
    net_push_srv_t *srv = &s_net_push_srv;
    int ret = -1;

    /* 清空控制块 */
    memset(srv, 0, sizeof(net_push_srv_t));
    srv->evt_sys_sub_id = -1;
    srv->evt_ai_sub_id = -1;
    srv->h264_enc = NULL;
    srv->h264_buf = NULL;

    /* 初始化互斥锁 */
    ret = pthread_mutex_init(&srv->mutex, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "互斥锁初始化失败");
        return -1;
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

    LOG_I(MODULE_TAG "网络推流服务初始化完成（H.264编码模式）");
    return 0;
}

// ==========================================================================
// @brief   模块自动初始化（系统启动自动加载）
// ============================================================================
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