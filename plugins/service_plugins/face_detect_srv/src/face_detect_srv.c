/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           face_detect_srv.c
 * @brief          人脸检测服务模块（DataBus V4.0 拉模式 + 事件唤醒版）
 * @details        1. 基于生产者事件通知唤醒，无CPU空耗
 *                 2. 纯拉模式获取最新视频帧，自动丢弃旧帧，适配AI降频
 *                 3. 物理隔离AI专属RGB总线，杜绝内存踩踏
 *                 4. 严格遵循DataBus V4.0 引用计数规范
 *                 5. 低功耗设计：事件触发处理，无忙等待
 * @author         Luo
 * @date           2026
 ******************************************************************************
 */

// ==========================================================================
// 【文件私有化宏定义】
// ==========================================================================
#define MODULE_NAME               "FACE_DETECT"
#define MODULE_TAG                "[FACE_DETECT]"

/* 数据总线名称 */
#define VIDEO_DATA_BUS            VIDEO_DATA_BUS_NAME                // 主摄像头YUYV总线
#define AI_RGB_DATA_BUS           AI_RGB_DATA_BUS_NAME               // AI专属RGB总线
#define CAPTURE_EVENT_BUS         SYS_EVENT_BUS_NAME        // 采集事件总线

/* 资源配置 */
#define AI_RGB_POOL_SIZE          4                       // AI串行处理，2帧足够
#define AI_RGB_MAX_SUBSCRIBERS    4
#define AI_MAX_FACES              MAX_FACES

/* AI模型参数 */
#define AI_MODEL_PATH             CONFIG_AI_MODEL_PATH
#define AI_INPUT_WIDTH            CONFIG_AI_INPUT_W
#define AI_INPUT_HEIGHT           CONFIG_AI_INPUT_H
#define AI_SCORE_THRESH           CONFIG_AI_SCORE_THRESH
#define AI_IOU_THRESH             CONFIG_AI_IOU_THRESH

/* 摄像头参数 */
#define CAPTURE_WIDTH             CONFIG_CAPTURE_WIDTH
#define CAPTURE_HEIGHT            CONFIG_CAPTURE_HEIGHT

/* 线程配置 */
#define FRAME_WAIT_TIMEOUT_MS     200                    // 等待超时时间

// AI RGB帧大小：宽度*高度*3字节(BGR)
#define AI_RGB_FRAME_SIZE         (CAPTURE_WIDTH * CAPTURE_HEIGHT * 3)

// ==========================================================================
// 头文件包含
// ==========================================================================
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "vision_ai_config.h"
#include "ai_model_mnn.hpp"
#include "initcall.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

/* =============================================================================
 * @brief 人脸检测服务控制块（线程安全 + 事件唤醒）
 * ============================================================================*/
typedef struct {
    ai_model_handle_t            *ai_model;              /* AI模型句柄 */

    /* 线程控制 */
    pthread_t                     work_thread;           /* 工作线程 */
    pthread_mutex_t               mutex;                 /* 条件变量互斥锁 */
    pthread_cond_t                cond;                  /* 事件唤醒条件变量 */
    bool                          thread_running;        /* 线程运行标志 */
    bool                          is_paused;             /* 服务暂停标志 */
    bool                          is_started;            /* 服务启动标志 */

    /* 事件订阅 */
    int                           evt_sys_sub_id;        /* 系统事件订阅ID */
    int                           evt_capture_sub_id;    /* 采集事件订阅ID */

    /* AI检测结果 */
    FaceInfo_C                    faces[AI_MAX_FACES];    /* 人脸信息数组 */
    int                           face_num;              /* 检测到人脸数量 */
} face_detect_srv_t;

/* 全局单例 */
static face_detect_srv_t s_face_srv;

/* =============================================================================
 * 静态函数声明
 * ============================================================================*/
static void  event_bus_cb(const event_t *event, void *user_data);
static void *face_work_thread(void *arg);
static int   face_srv_start(void);
static void  face_srv_cleanup(void);
static int   face_srv_init(void);
static int   face_srv_auto_init(void);

/* =============================================================================
 * @brief   事件总线回调（系统事件 + 采集帧就绪事件）
 * @param   event: 事件结构体
 * @param   user_data: 用户参数
 * ============================================================================*/
static void event_bus_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    face_detect_srv_t *srv = &s_face_srv;

    switch (event->type)
    {
        /* 采集帧就绪：唤醒AI线程处理（核心：低功耗触发） */
        case EVENT_TYPE_CAPTURE_PROTO_READY:
            if (srv->thread_running && !srv->is_paused)
            {
                pthread_mutex_lock(&srv->mutex);
                pthread_cond_signal(&srv->cond);  /* 唤醒工作线程 */
                pthread_mutex_unlock(&srv->mutex);
            }
            break;

        /* 系统核心就绪 */
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG "系统核心初始化完成");
            break;

        /* 系统暂停 */
        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG "服务进入暂停状态");
            srv->is_paused = true;
            break;

        /* 系统恢复 */
        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started)
            {
                face_srv_start();
                srv->is_started = true;
            }
            else
            {
                srv->is_paused = false;
                LOG_I(MODULE_TAG "服务恢复运行");
            }
            break;

        /* 系统停止/关机/异常 */
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
        case EVENT_TYPE_SYS_ERROR:
            face_srv_cleanup();
            break;

        default:
            break;
    }
}

// 项目中可能存在的封装函数（非标准）
int pthread_cond_timedwait_ms(pthread_cond_t *cond, 
                              pthread_mutex_t *mutex, 
                              uint32_t timeout_ms) 
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);  // 获取当前时间

    // 计算超时绝对时间（处理纳秒溢出）
    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    // 调用标准 API
    return pthread_cond_timedwait(cond, mutex, &ts);
}

/* =============================================================================
 * @brief   AI工作线程（拉模式 + 事件唤醒，无CPU空耗）
 * @param   arg: 线程参数
 * @return  线程退出码
 * @note    1. 等待采集事件唤醒，不忙等
 *          2. 拉取最新帧，自动丢弃旧帧
 *          3. 严格DataBus引用计数管理
 * ============================================================================*/
static void *face_work_thread(void *arg)
{
    (void)arg;
    face_detect_srv_t *srv = &s_face_srv;
    data_bus_item_handle_t yuyv_item = NULL;
    data_bus_item_handle_t rgb_item = NULL;
    data_bus_item_handle_t ai_result_item = NULL;
    int ret;

    LOG_I(MODULE_TAG "AI工作线程启动【拉模式+事件唤醒】");

    while (srv->thread_running)
    {
        /* 暂停状态：等待 */
        if (srv->is_paused)
        {
            usleep(FRAME_WAIT_TIMEOUT_MS * 1000);
            continue;
        }

        /* ============== 核心：等待采集事件唤醒（低功耗） ============== */
        pthread_mutex_lock(&srv->mutex);
        pthread_cond_timedwait_ms(&srv->cond, &srv->mutex, FRAME_WAIT_TIMEOUT_MS);
        pthread_mutex_unlock(&srv->mutex);

        /* ============== 拉模式：获取最新摄像头帧 ============== */
        ret = data_bus_pull_latest(VIDEO_DATA_BUS, DATA_TYPE_VIDEO, &yuyv_item);
        if (ret != DATA_BUS_OK || !yuyv_item)
        {
            continue;
        }

        /* ============== 申请AI专用RGB帧 ============== */
        ret = data_bus_alloc(AI_RGB_DATA_BUS,
                             DATA_TYPE_VIDEO_RGB,
                             AI_RGB_FRAME_SIZE,
                             MODULE_NAME,
                             &rgb_item);
        if (ret != DATA_BUS_OK)
        {
            LOG_W(MODULE_TAG "AI RGB总线无空闲帧");
            data_bus_release(yuyv_item);
            yuyv_item = NULL;
            continue;
        }

        /* ============== 获取数据指针 ============== */
        const uint8_t *yuyv_data = data_bus_get_readonly_ptr(yuyv_item);
        uint8_t *rgb_data = data_bus_get_writable_ptr(rgb_item);

        /* ============== AI推理（YUYV转RGB + 人脸检测） ============== */
        srv->face_num = 0;
        memset(srv->faces, 0, sizeof(srv->faces));
        ret = ai_model_mnn_infer_yuyv(yuyv_data,
                                      CAPTURE_WIDTH,
                                      CAPTURE_HEIGHT,
                                      rgb_data,
                                      srv->faces,
                                      AI_MAX_FACES,
                                      &srv->face_num);

        /* 推理异常：释放资源并跳过 */
        if (ret != MNN_FACE_OK)
        {
            LOG_E(MODULE_TAG "AI推理失败，错误码:%d", ret);
            data_bus_release(rgb_item);
            data_bus_release(yuyv_item);
            rgb_item = yuyv_item = NULL;
            continue;
        }
        
        /* ============== 人脸坐标映射 ============== */
        if (srv->face_num > 0)
        {
            for (int i = 0; i < srv->face_num; i++)
            {
                ai_model_mnn_map_face(&srv->faces[i], CAPTURE_WIDTH, CAPTURE_HEIGHT);
            }
            LOG_I(MODULE_TAG "检测到 %d 张人脸", srv->face_num);
        }
        else
        {
            LOG_D(MODULE_TAG "未检测到人脸");
        }
        data_bus_push(AI_RGB_DATA_BUS, rgb_item);
        /* ============== 仅有人脸时：发布AI结果到总线 ============== */
        if (srv->face_num > 0)
        {
            
            ret = data_bus_alloc(VIDEO_DATA_BUS,
                                 DATA_TYPE_AI_RESULT,
                                 sizeof(FaceInfo_C) * srv->face_num,
                                 MODULE_NAME,
                                 &ai_result_item);
            if (ret == DATA_BUS_OK)
            {
                memcpy(data_bus_get_writable_ptr(ai_result_item),
                       srv->faces,
                       sizeof(FaceInfo_C) * srv->face_num);

            }
        }

        /* ============== 释放所有帧（严格配对引用计数） ============== */
        data_bus_release(rgb_item);   /* 释放AI临时RGB帧 */
        data_bus_release(yuyv_item);  /* 释放摄像头源帧 */
        data_bus_release(ai_result_item);
        rgb_item = yuyv_item = NULL;

        /* 发布AI处理完成事件 */
        event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_PROCESS_DONE, MODULE_NAME);
    }

    LOG_I(MODULE_TAG "AI工作线程安全退出");
    return NULL;
}

/* =============================================================================
 * @brief   服务启动函数
 * @return  0:成功 负数:失败
 * ============================================================================*/
static int face_srv_start(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    int ret = -1;

    /* 初始化条件变量（事件唤醒核心） */
    ret = pthread_cond_init(&srv->cond, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "条件变量初始化失败");
        return -1;
    }

    /* 创建工作线程 */
    srv->thread_running = true;
    srv->is_paused = false;
    ret = pthread_create(&srv->work_thread, NULL, face_work_thread, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "工作线程创建失败");
        pthread_cond_destroy(&srv->cond);
        srv->thread_running = false;
        return -1;
    }

    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_READY, MODULE_NAME);
    LOG_I(MODULE_TAG "人脸检测服务启动成功");
    return 0;
}

/* =============================================================================
 * @brief   服务资源清理（安全退出）
 * ============================================================================*/
static void face_srv_cleanup(void)
{
    face_detect_srv_t *srv = &s_face_srv;

    LOG_W(MODULE_TAG "开始释放所有资源");

    /* 停止线程 */
    srv->thread_running = false;
    srv->is_paused = true;

    /* 唤醒阻塞线程，确保退出 */
    pthread_mutex_lock(&srv->mutex);
    pthread_cond_signal(&srv->cond);
    pthread_mutex_unlock(&srv->mutex);

    if (srv->work_thread > 0)
    {
        pthread_join(srv->work_thread, NULL);
    }

    /* 取消事件订阅 */
    if (srv->evt_sys_sub_id >= 0)
    {
        event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->evt_sys_sub_id);
    }
    if (srv->evt_capture_sub_id >= 0)
    {
        event_bus_unsubscribe(CAPTURE_EVENT_BUS, srv->evt_capture_sub_id);
    }

    /* 销毁条件变量 */
    pthread_cond_destroy(&srv->cond);
    pthread_mutex_destroy(&srv->mutex);

    /* 销毁AI模型 + AI总线 */
    if (srv->ai_model)
    {
        ai_model_destroy(srv->ai_model);
        srv->ai_model = NULL;
    }
    data_bus_deinit(AI_RGB_DATA_BUS);

    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG "所有资源释放完成");
}

/* =============================================================================
 * @brief   服务初始化
 * @return  0:成功 负数:失败
 * ============================================================================*/
static int face_srv_init(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    int ret = -1;

    /* 清空控制块 */
    memset(srv, 0, sizeof(face_detect_srv_t));
    srv->evt_sys_sub_id = -1;
    srv->evt_capture_sub_id = -1;

    /* 初始化互斥锁 */
    ret = pthread_mutex_init(&srv->mutex, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "互斥锁初始化失败");
        return -1;
    }

    /* 初始化AI专属RGB总线 */
    data_bus_config_t ai_bus_cfg = {
        .name = AI_RGB_DATA_BUS,
        .max_item_size = AI_RGB_FRAME_SIZE,
        .max_items = AI_RGB_POOL_SIZE,
        .max_subscribers = AI_RGB_MAX_SUBSCRIBERS
    };
    ret = data_bus_init(&ai_bus_cfg);
    if (ret != DATA_BUS_OK)
    {
        LOG_E(MODULE_TAG "AI RGB总线初始化失败");
        pthread_mutex_destroy(&srv->mutex);
        return -1;
    }

    /* 初始化AI模型 */
    ai_model_config_t ai_cfg = {
        .model_path    = AI_MODEL_PATH,
        .input_width   = AI_INPUT_WIDTH,
        .input_height  = AI_INPUT_HEIGHT,
        .score_thresh  = AI_SCORE_THRESH,
        .iou_thresh    = AI_IOU_THRESH
    };
    srv->ai_model = ai_model_mnn_create(&ai_cfg);
    if (!srv->ai_model || ai_model_init(srv->ai_model) != AI_MODEL_OK)
    {
        LOG_E(MODULE_TAG "AI模型初始化失败");
        data_bus_deinit(AI_RGB_DATA_BUS);
        pthread_mutex_destroy(&srv->mutex);
        return -1;
    }

    /* 订阅：系统事件总线 */
    event_subscriber_t sys_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = event_bus_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_sys_sub_id = event_bus_subscribe(SYS_EVENT_BUS_NAME, &sys_sub);

    /* 订阅：采集帧就绪事件（核心唤醒源） */
    event_subscriber_t cap_sub = {
        .event_type = EVENT_TYPE_CAPTURE_PROTO_READY,
        .callback = event_bus_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_capture_sub_id = event_bus_subscribe(CAPTURE_EVENT_BUS, &cap_sub);

    if (srv->evt_sys_sub_id < 0 || srv->evt_capture_sub_id < 0)
    {
        LOG_E(MODULE_TAG "事件总线订阅失败");
        face_srv_cleanup();
        return -1;
    }

    LOG_I(MODULE_TAG "人脸检测服务初始化完成");
    return 0;
}

/* =============================================================================
 * @brief   模块自动初始化（系统启动时自动加载）
 * ============================================================================*/
static int face_srv_auto_init(void)
{
    if (face_srv_init() != 0)
    {
        return -1;
    }
    LOG_I(MODULE_TAG "模块自动加载完成，等待系统启动指令");
    return 0;
}

MODULE_INIT_LEVEL(INIT_SERVICE, face_srv_auto_init);

/******************************* End of file **********************************/