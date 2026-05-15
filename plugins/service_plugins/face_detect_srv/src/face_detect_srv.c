/* SPDX-License-Identifier: MIT */
/**
  ******************************************************************************
  * @file           face_detect_srv.c
  * @brief          人脸检测服务模块
  * @details        基于事件总线接收系统控制指令
  *                 基于数据总线零拷贝订阅视频帧数据
  *                 初始化后等待系统指令启动，与采集服务完全解耦
  * @author         Luo
  * @date           2025
  ******************************************************************************
  */

#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "vision_ai_config.h"
#include "ai_model_mnn.hpp"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* =============================================================================
 * 宏定义配置（严格使用全局统一配置，无硬编码）
 * ============================================================================*/
#define MODULE_NAME               "FACE_DETECT"        /**< 模块名称 */
#define MODULE_TAG                "[FACE_DETECT]"     /**< 日志标签 */

/* 总线名称（全局统一宏定义） */
#define SYS_EVENT_BUS             SYS_EVENT_BUS_NAME   /**< 系统事件总线 */
#define VIDEO_DATA_BUS            "video"              /**< 视频数据总线 */

/* 服务私有配置 */
#define FACE_FRAME_WAIT_US        20000                /**< 帧等待休眠时间(20ms) */

/* =============================================================================
 * 私有类型定义
 * ============================================================================*/
/**
 * @brief 人脸检测服务控制结构体
 * @note  单例设计，管理服务所有资源与运行状态
 */
typedef struct {
    ai_model_handle_t            *ai_model;           /**< AI模型句柄 */
    FaceInfo_C                    faces[MAX_FACES];    /**< 人脸检测结果数组 */
    int                           face_num;            /**< 检测到的人脸数量 */

    pthread_t                     work_thread;         /**< 工作线程ID */
    bool                          thread_running;     /**< 线程运行标志 */
    bool                          is_paused;          /**< 服务暂停标志 */
    bool                          is_started;         /**< 服务已启动标志 */
    pthread_mutex_t               lock;                /**< 线程互斥锁 */

    int                           evt_sub_id;          /**< 事件总线订阅ID */
    data_bus_subscription_handle_t data_sub;           /**< 数据总线订阅句柄 */
} face_detect_t;

/* =============================================================================
 * 全局静态变量（单例）
 * ============================================================================*/
static face_detect_t s_face_detect;

/* =============================================================================
 * 静态函数声明
 * ============================================================================*/
static inline void _face_lock(void);
static inline void _face_unlock(void);
static void face_detect_cleanup(void);
static void _data_bus_frame_cb(data_bus_item_handle_t item, void *user_data);
static void _event_bus_cb(const event_t *event, void *user_data);
static void *face_work_thread(void *arg);
static int face_detect_start(void);
static int face_detect_init(void);
static int _face_detect_auto_init(void);

/* =============================================================================
 * 函数实现
 * ============================================================================*/

/**
 * @brief  线程安全加锁
 * @retval 无
 */
static inline void _face_lock(void)
{
    pthread_mutex_lock(&s_face_detect.lock);
}

/**
 * @brief  线程安全解锁
 * @retval 无
 */
static inline void _face_unlock(void)
{
    pthread_mutex_unlock(&s_face_detect.lock);
}

/**
 * @brief  数据总线视频帧回调
 * @param  item: 数据总线帧句柄
 * @param  user_data: 用户自定义参数
 * @details 推模式接收视频帧，零拷贝处理
 * @retval 无
 */
static void _data_bus_frame_cb(data_bus_item_handle_t item, void *user_data)
{
    (void)user_data;
    face_detect_t *srv = &s_face_detect;

    /* 服务未运行/暂停状态，直接释放帧数据 */
    if (!srv->thread_running || srv->is_paused)
    {
        data_bus_release(item);
        return;
    }

    /* 零拷贝获取只读数据指针 */
    const uint8_t *frame_data = (const uint8_t *)data_bus_get_readonly_ptr(item);
    if (!frame_data)
    {
        data_bus_release(item);
        return;
    }

    /* 执行AI人脸检测推理 */
    srv->face_num = 0;
    ai_model_mnn_infer_yuyv(frame_data,
                            CONFIG_CAPTURE_WIDTH,
                            CONFIG_CAPTURE_HEIGHT,
                            srv->faces,
                            MAX_FACES,
                            &srv->face_num);

    /* 打印检测结果 */
    if (srv->face_num > 0)
    {
        LOG_I(MODULE_TAG " 检测到 %d 张人脸", srv->face_num);
        for (int i = 0; i < srv->face_num; i++)
        {
            LOG_I(MODULE_TAG " 人脸[%d]: (%.1f,%.1f)-(%.1f,%.1f) 置信度:%.2f",
                  i + 1,
                  srv->faces[i].x1, srv->faces[i].y1,
                  srv->faces[i].x2, srv->faces[i].y2,
                  srv->faces[i].score);
        }
    }

    /* 发布检测完成事件 */
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_PROCESS_DONE, MODULE_NAME);

    /* 释放数据总线帧 */
    data_bus_release(item);
}

/**
 * @brief  系统事件总线回调
 * @param  event: 系统事件
 * @param  user_data: 用户自定义参数
 * @details 响应系统启停、暂停、恢复指令
 * @retval 无
 */
static void _event_bus_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    face_detect_t *srv = &s_face_detect;

    switch (event->type)
    {
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG " 收到系统核心就绪事件");
            break;

        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG " 收到暂停指令，停止处理视频帧");
            srv->is_paused = true;
            break;

        /* 核心：仅收到RESUME指令，才启动服务 */
        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started)
            {
                LOG_I(MODULE_TAG " 收到启动指令，开始人脸检测服务");
                face_detect_start();
                srv->is_started = true;
            }
            else
            {
                LOG_I(MODULE_TAG " 收到恢复指令，继续处理视频帧");
                srv->is_paused = false;
            }
            break;

        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            LOG_I(MODULE_TAG " 收到停止指令，释放所有资源");
            face_detect_cleanup();
            break;

        case EVENT_TYPE_SYS_ERROR:
            LOG_E(MODULE_TAG " 收到系统错误，强制清理资源");
            face_detect_cleanup();
            break;

        default:
            break;
    }
}

/**
 * @brief  人脸检测工作线程
 * @param  arg: 线程参数
 * @details 线程空转，所有处理由数据总线回调完成
 * @retval 线程返回值
 */
static void *face_work_thread(void *arg)
{
    (void)arg;
    face_detect_t *srv = &s_face_detect;

    LOG_I(MODULE_TAG " 工作线程启动，等待视频帧数据...");

    while (srv->thread_running)
    {
        if (srv->is_paused)
        {
            usleep(FACE_FRAME_WAIT_US);
            continue;
        }
        usleep(FACE_FRAME_WAIT_US);
    }

    LOG_I(MODULE_TAG " 工作线程退出");
    return NULL;
}

/**
 * @brief  人脸检测服务启动
 * @details 订阅总线、创建线程，仅指令触发
 * @retval 0成功，负数失败
 */
static int face_detect_start(void)
{
    face_detect_t *srv = &s_face_detect;
    int ret = -1;

    /* 1. 订阅系统事件总线 */
    event_subscriber_t evt_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _event_bus_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_sub_id = event_bus_subscribe(SYS_EVENT_BUS, &evt_sub);
    if (srv->evt_sub_id < 0)
    {
        LOG_E(MODULE_TAG " 订阅系统事件总线失败");
        return -1;
    }

    /* 2. 订阅视频数据总线（零拷贝推模式） */
    ret = data_bus_subscribe(VIDEO_DATA_BUS,
                             DATA_TYPE_VIDEO_FRAME,
                             _data_bus_frame_cb,
                             NULL,
                             &srv->data_sub);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG " 订阅视频数据总线失败");
        event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_sub_id);
        srv->evt_sub_id = -1;
        return -1;
    }

    /* 3. 启动工作线程 */
    srv->thread_running = true;
    ret = pthread_create(&srv->work_thread, NULL, face_work_thread, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG " 创建工作线程失败");
        data_bus_unsubscribe(VIDEO_DATA_BUS, &srv->data_sub);
        event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_sub_id);
        srv->evt_sub_id = -1;
        srv->thread_running = false;
        return -1;
    }

    /* 4. 发布服务就绪事件 */
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_READY, MODULE_NAME);
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_RUNNING, MODULE_NAME);
    LOG_I(MODULE_TAG " 服务启动成功");

    return 0;
}

/**
 * @brief  服务资源清理
 * @details 释放线程、总线、AI模型、锁等所有资源
 * @retval 无
 */
static void face_detect_cleanup(void)
{
    face_detect_t *srv = &s_face_detect;

    LOG_W(MODULE_TAG " 开始全量资源释放...");

    /* 停止线程 */
    srv->thread_running = false;
    srv->is_paused = true;
    if (srv->work_thread > 0)
    {
        pthread_join(srv->work_thread, NULL);
        srv->work_thread = 0;
    }

    /* 取消事件订阅 */
    if (srv->evt_sub_id >= 0)
    {
        event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_sub_id);
        srv->evt_sub_id = -1;
    }

    /* 取消数据订阅 */
    data_bus_unsubscribe(VIDEO_DATA_BUS, &srv->data_sub);

    /* 销毁AI模型 */
    if (srv->ai_model)
    {
        ai_model_destroy(srv->ai_model);
        srv->ai_model = NULL;
    }

    /* 销毁互斥锁 */
    pthread_mutex_destroy(&srv->lock);

    /* 发布停止事件 */
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG " 所有资源释放完成");
}

/**
 * @brief  人脸检测服务初始化
 * @details 仅初始化模型、锁、状态，不启动任何业务
 * @retval 0成功，负数失败
 */
static int face_detect_init(void)
{
    face_detect_t *srv = &s_face_detect;
    int ret = -1;

    /* 清空结构体 */
    memset(srv, 0, sizeof(face_detect_t));

    /* 初始化状态 */
    srv->is_paused = false;
    srv->is_started = false;
    srv->evt_sub_id = -1;

    /* 初始化互斥锁 */
    ret = pthread_mutex_init(&srv->lock, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG " 互斥锁初始化失败");
        return -1;
    }

    /* 初始化AI模型 */
    ai_model_config_t ai_cfg = {
        .model_path = CONFIG_AI_MODEL_PATH,
        .input_width = CONFIG_AI_INPUT_W,
        .input_height = CONFIG_AI_INPUT_H,
        .score_thresh = CONFIG_AI_SCORE_THRESH,
        .iou_thresh = CONFIG_AI_IOU_THRESH
    };
    srv->ai_model = ai_model_mnn_create(&ai_cfg);
    if (!srv->ai_model)
    {
        LOG_E(MODULE_TAG " AI模型初始化失败");
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    LOG_I(MODULE_TAG " 服务初始化完成");
    return 0;
}

/**
 * @brief  模块自动初始化入口
 * @details 系统启动时自动调用，仅初始化，等待指令
 * @retval 0成功，负数失败
 */
#include "initcall.h"
static int _face_detect_auto_init(void)
{
    if (face_detect_init() != 0)
    {
        return -1;
    }
    LOG_I(MODULE_TAG " 自动加载完成，等待系统启动指令");
    return 0;
}

/* 注册到系统初始化段 */
MODULE_INIT(_face_detect_auto_init);

/******************************* End of file **********************************/