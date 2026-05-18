/* SPDX-License-Identifier: MIT */
/**
  ******************************************************************************
  * @file           face_detect_srv.c
  * @brief          人脸检测服务模块
  * @details        【架构规范版 - 适配最新FrameLink】
  *                 1. 数据总线回调：仅帧引用+入队，零耗时操作
  *                 2. 严格遵循FrameLink数据链路层消费者接口规范
  *                 3. 工作线程：独立处理AI耗时推理，与总线完全解耦
  *                 4. 原子引用计数自动管理，多服务安全共享视频帧
  *                 5. 层级边界：采集服务→DataBus→FrameLink→AI服务 清晰解耦
  *                 6. AI模型：自动适配640x360原始帧，内部缩放至320x240最优分辨率
  *                 7. 不透明句柄设计：禁止直接访问帧内部成员
  * @author         Luo
  * @date           2026
  ******************************************************************************
  */

// ==========================================================================
// 【文件私有化宏定义 - 全部来源于全局配置】
// 禁止硬编码，所有参数统一从 vision_ai_config.h 引入
// ==========================================================================
#define MODULE_NAME               "FACE_DETECT"        /**< 模块唯一名称 */
#define MODULE_TAG                "[FACE_DETECT]"      /**< 日志输出标签 */

/* 全局总线/链路名称（系统统一约定） */
#define SYS_EVENT_BUS             SYS_EVENT_BUS_NAME   /**< 系统事件总线名称 */
#define VIDEO_DATA_BUS            "video"              /**< 视频数据总线名称 */
#define FRAME_LINK_NAME           "main_cam"           /**< 帧链路名称（与采集服务一致） */

/* AI模型配置（全局训练最优参数） */
#define AI_MODEL_PATH             CONFIG_AI_MODEL_PATH
#define AI_INPUT_WIDTH            CONFIG_AI_INPUT_W     // 320（模型训练分辨率）
#define AI_INPUT_HEIGHT           CONFIG_AI_INPUT_H    // 240（模型训练分辨率）
#define AI_SCORE_THRESH           CONFIG_AI_SCORE_THRESH
#define AI_IOU_THRESH             CONFIG_AI_IOU_THRESH
#define MAX_FACES                 MAX_FACES             /**< 单帧最大检测人脸数 */

/* 摄像头采集参数（全局配置） */
#define CAPTURE_WIDTH             CONFIG_CAPTURE_WIDTH  // 640（原始分辨率）
#define CAPTURE_HEIGHT            CONFIG_CAPTURE_HEIGHT // 360（原始分辨率）

/* 服务私有配置 */
#define FRAME_WAIT_TIMEOUT_US     20000                /**< 帧等待超时时间(20ms) */
#define AI_PROCESS_QUEUE_SIZE     8                    /**< AI内部缓冲队列长度 */

// ==========================================================================
// 头文件包含（按系统依赖层级排序）
// ==========================================================================
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "frame_link.h"
#include "vision_ai_config.h"
#include "ai_model_mnn.hpp"
#include "initcall.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

/* =============================================================================
 * 私有类型定义
 * ============================================================================*/
/**
 * @brief 人脸检测服务控制块（单例模式）
 * @note  管理服务所有资源、线程、队列、总线句柄
 * @note  适配FrameLink不透明句柄：frame_handle_t
 */
typedef struct {
    ai_model_handle_t            *ai_model;           /**< AI模型句柄 */
    FaceInfo_C                    faces[MAX_FACES];    /**< 人脸检测结果缓存 */
    int                           face_num;            /**< 实际检测人脸数量 */

    pthread_t                     work_thread;         /**< AI工作线程ID */
    bool                          thread_running;      /**< 线程运行标志 */
    bool                          is_paused;           /**< 服务暂停标志 */
    bool                          is_started;          /**< 服务启动标志 */
    pthread_mutex_t               lock;                /**< 线程安全互斥锁 */

    /* AI内部缓冲队列：解耦数据总线回调与耗时推理 */
    frame_handle_t                frame_queue[AI_PROCESS_QUEUE_SIZE];
    uint32_t                      queue_front;         /**< 队列头指针 */
    uint32_t                      queue_rear;          /**< 队列尾指针 */
    uint32_t                      queue_count;         /**< 队列元素计数 */

    int                           evt_sub_id;          /**< 事件总线订阅ID */
    data_bus_subscription_handle_t data_sub;           /**< 数据总线订阅句柄 */
} face_detect_srv_t;

/* =============================================================================
 * 全局静态单例（服务唯一实例）
 * ============================================================================*/
static face_detect_srv_t s_face_srv;

/* =============================================================================
 * 静态函数声明
 * ============================================================================*/
static void    face_srv_lock(void);
static void    face_srv_unlock(void);
static bool    face_queue_enqueue(frame_handle_t frame);
static frame_handle_t face_queue_dequeue(void);
static void    face_srv_cleanup(void);
static void    data_bus_frame_cb(data_bus_item_handle_t item, void *user_data);
static void    event_bus_cb(const event_t *event, void *user_data);
static void   *face_work_thread(void *arg);
static int     face_srv_start(void);
static int     face_srv_init(void);
static int     face_srv_auto_init(void);

/* =============================================================================
 * 线程安全工具函数
 * ============================================================================*/
/**
 * @brief  服务互斥锁加锁
 * @return 无
 */
static inline void face_srv_lock(void)
{
    pthread_mutex_lock(&s_face_srv.lock);
}

/**
 * @brief  服务互斥锁解锁
 * @return 无
 */
static inline void face_srv_unlock(void)
{
    pthread_mutex_unlock(&s_face_srv.lock);
}

/* =============================================================================
 * AI内部帧队列操作（线程安全）
 * ============================================================================*/
/**
 * @brief  帧入队（数据总线→工作线程缓冲）
 * @param  frame: FrameLink不透明帧句柄
 * @retval true:入队成功 false:队列满
 */
static bool face_queue_enqueue(frame_handle_t frame)
{
    bool ret = false;

    face_srv_lock();
    if (s_face_srv.queue_count < AI_PROCESS_QUEUE_SIZE)
    {
        s_face_srv.frame_queue[s_face_srv.queue_rear] = frame;
        s_face_srv.queue_rear = (s_face_srv.queue_rear + 1) % AI_PROCESS_QUEUE_SIZE;
        s_face_srv.queue_count++;
        ret = true;
    }
    face_srv_unlock();

    return ret;
}

/**
 * @brief  帧出队（工作线程获取待处理帧）
 * @retval frame_handle_t:帧句柄 NULL:队列为空
 */
static frame_handle_t face_queue_dequeue(void)
{
    frame_handle_t frame = NULL;

    face_srv_lock();
    if (s_face_srv.queue_count > 0)
    {
        frame = s_face_srv.frame_queue[s_face_srv.queue_front];
        s_face_srv.queue_front = (s_face_srv.queue_front + 1) % AI_PROCESS_QUEUE_SIZE;
        s_face_srv.queue_count--;

    }
    face_srv_unlock();

    return frame;
}
/* =============================================================================
 * 数据总线回调函数【核心：零耗时、仅引用、不入队】
 * ============================================================================*/
/**
 * @brief  视频数据总线回调
 * @param  item: 数据总线项（存储frame_handle_t句柄）
 * @param  user_data: 用户自定义参数
 * @details 严格遵守FrameLink架构规范：
 *          1. 不执行任何耗时操作（无推理、无打印、无拷贝）
 *          2. 仅做：获取句柄 + 引用帧 + 入队缓冲 + 释放总线项
 *          3. 立即返回，不阻塞总线调度
 * @return 无
 */
static void data_bus_frame_cb(data_bus_item_handle_t item, void *user_data)
{
    (void)user_data;
    face_detect_srv_t *srv = &s_face_srv;

    // 服务未运行/暂停，直接丢弃帧
    if (!srv->thread_running || srv->is_paused)
    {
        data_bus_release(item);
        return;
    }

    // 从总线获取FrameLink不透明句柄（零拷贝）
    frame_handle_t bus_frame = NULL;
    const void *bus_ptr = data_bus_get_readonly_ptr(item);
    if (!bus_ptr)
    {
        data_bus_release(item);
        return;
    }
    bus_frame = *(frame_handle_t *)bus_ptr;
    LOG_I(MODULE_TAG "received frame from bus, frame_handle=%p", bus_frame);
    // FrameLink规范：通过总线句柄引用帧（引用计数+1，防止被回收）
    frame_handle_t proc_frame = NULL;
    if (frame_link_consumer_get_by_bus(FRAME_LINK_NAME, bus_frame, &proc_frame) != FL_OK)
    {
        LOG_E(MODULE_TAG"frame_link_consumer_get_by_bus failed for frame_handle=%p", bus_frame);
        data_bus_release(item);
        return;
    }

    // 入队缓冲队列，失败则释放帧引用
    if (!face_queue_enqueue(proc_frame))
    {
        LOG_E(MODULE_TAG "face_queue_enqueue failed for frame_handle=%p", proc_frame);
        frame_link_consumer_put(proc_frame);
    }
    // 释放总线包装项（不释放帧本体）
    data_bus_release(item);
}

/* =============================================================================
 * 系统事件总线回调
 * ============================================================================*/
/**
 * @brief  系统事件处理回调
 * @param  event: 系统事件结构体
 * @param  user_data: 用户自定义参数
 * @return 无
 */
static void event_bus_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    face_detect_srv_t *srv = &s_face_srv;

    switch (event->type)
    {
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG " 收到系统核心就绪事件");
            break;

        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG " 收到暂停指令，停止视频帧处理");
            srv->is_paused = true;
            break;

        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started)
            {
                LOG_I(MODULE_TAG " 收到启动指令，初始化人脸检测服务");
                face_srv_start();
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
            LOG_I(MODULE_TAG " 收到停止指令，开始释放所有资源");
            face_srv_cleanup();
            break;

        case EVENT_TYPE_SYS_ERROR:
            LOG_E(MODULE_TAG " 收到系统错误，强制资源清理");
            face_srv_cleanup();
            break;

        default:
            break;
    }
}

/* =============================================================================
 * AI工作线程（耗时推理核心）
 * ============================================================================*/
/**
 * @brief  AI人脸检测工作线程
 * @param  arg: 线程参数
 * @details 核心流程：
 *          1. 从缓冲队列获取帧句柄
 *          2. 通过FrameLink接口获取只读数据指针
 *          3. 调用AI模型推理（640x360→320x240自动缩放）
 *          4. 人脸坐标映射至原始分辨率
 *          5. 释放帧引用（引用计数-1）
 *          6. 发布处理完成事件
 * @return 线程返回值
 */
static void *face_work_thread(void *arg)
{
    (void)arg;
    face_detect_srv_t *srv = &s_face_srv;
    frame_handle_t proc_frame = NULL;
    frame_info_t frame_info = {0};

    LOG_I(MODULE_TAG " 工作线程启动，等待视频帧数据...");

    while (srv->thread_running)
    {
        // 暂停状态：休眠等待
        if (srv->is_paused)
        {
            usleep(FRAME_WAIT_TIMEOUT_US);
            continue;
        }

        // 获取待处理帧句柄
        proc_frame = face_queue_dequeue();
        if (!proc_frame)
        {
            usleep(FRAME_WAIT_TIMEOUT_US);
            continue;
        }

        // 清空上一帧结果
        srv->face_num = 0;
        memset(srv->faces, 0, sizeof(srv->faces));

        // ====================== FrameLink规范：获取帧只读数据 ======================
        const uint8_t *frame_data = frame_get_readonly_ptr(proc_frame);
        // 获取帧元数据（帧ID、分辨率等）
        frame_get_info(proc_frame, &frame_info);
        LOG_D(MODULE_TAG " 处理帧ID=%p | 分辨率=%ux%u", frame_data, frame_info.width, frame_info.height);
        // ====================== AI核心推理 ======================
        // 直接传入摄像头原始YUYV数据，模型内部自动缩放至320x240
        int ret = ai_model_mnn_infer_yuyv((void *)frame_data,
                                CAPTURE_WIDTH,
                                CAPTURE_HEIGHT,
                                srv->faces,
                                MAX_FACES,
                                &srv->face_num);

        if (ret != 0)
        {
            LOG_E(MODULE_TAG " AI推理失败");
            frame_link_consumer_put(proc_frame);
            continue;
        }

        // 人脸坐标映射：模型输出→原始摄像头分辨率（必须调用）
        for (int i = 0; i < srv->face_num; i++)
        {
            ai_model_mnn_map_face(&srv->faces[i], CAPTURE_WIDTH, CAPTURE_HEIGHT);
        }

        // 日志输出检测结果
        if (srv->face_num > 0)
        {
            LOG_I(MODULE_TAG " 帧[%u] 检测到 %d 张人脸", frame_info.frame_id, srv->face_num);
        }
        else 
        {   
            LOG_I(MODULE_TAG " 帧[%u] 未检测到人脸", frame_info.frame_id);
        }

        // ====================== FrameLink规范：释放帧引用 ======================
        frame_link_consumer_put(proc_frame);

        // 发布AI处理完成事件
        event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_PROCESS_DONE, MODULE_NAME);
    }

    LOG_I(MODULE_TAG " 工作线程安全退出");
    return NULL;
}

/* =============================================================================
 * 服务启动函数
 * ============================================================================*/
/**
 * @brief  启动人脸检测服务
 * @details 订阅数据总线 + 创建工作线程
 * @retval 0:成功 -1:失败
 */
static int face_srv_start(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    int ret = -1;

    // 订阅视频数据总线
    ret = data_bus_subscribe(VIDEO_DATA_BUS,
                             DATA_TYPE_VIDEO_FRAME,
                             data_bus_frame_cb,
                             NULL,
                             &srv->data_sub);
    if (ret != 0)
    {
        // 增加详细错误信息
        if (ret == -1) LOG_E("订阅失败：参数错误/总线未初始化");
        else if (ret == -2) LOG_E("订阅失败：订阅者数量已满");
        else LOG_E("订阅失败：未知错误码%d", ret);
        return ret;
    }

    // 启动AI工作线程
    srv->thread_running = true;
    srv->is_paused = false;
    ret = pthread_create(&srv->work_thread, NULL, face_work_thread, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG " AI工作线程创建失败");
        data_bus_unsubscribe(VIDEO_DATA_BUS, &srv->data_sub);
        srv->thread_running = false;
        return -1;
    }

    // 发布服务就绪事件
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_READY, MODULE_NAME);
    LOG_I(MODULE_TAG " 服务启动成功，遵循最新FrameLink规范运行");

    return 0;
}

/* =============================================================================
 * 服务资源清理函数
 * ============================================================================*/
/**
 * @brief  服务全量资源清理
 * @details 停止线程 → 清空队列释放帧 → 取消订阅 → 销毁模型 → 释放锁
 * @return 无
 */
static void face_srv_cleanup(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    frame_handle_t frame = NULL;

    LOG_W(MODULE_TAG " 开始安全释放所有资源...");

    // 1. 停止工作线程
    srv->thread_running = false;
    srv->is_paused = true;
    if (srv->work_thread > 0)
    {
        pthread_join(srv->work_thread, NULL);
        srv->work_thread = 0;
    }

    // 2. 清空缓冲队列，释放所有未处理帧（FrameLink规范）
    while ((frame = face_queue_dequeue()) != NULL)
    {
        frame_link_consumer_put(frame);
    }

    // 3. 取消总线订阅
    if (srv->evt_sub_id >= 0)
    {
        event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_sub_id);
        srv->evt_sub_id = -1;
    }
    data_bus_unsubscribe(VIDEO_DATA_BUS, &srv->data_sub);

    // 4. 销毁AI模型
    if (srv->ai_model)
    {
        ai_model_destroy(srv->ai_model);
        srv->ai_model = NULL;
    }

    // 5. 销毁互斥锁
    pthread_mutex_destroy(&srv->lock);

    // 6. 发布服务停止事件
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG " 所有资源释放完成，帧内存安全回收");
}

/* =============================================================================
 * 服务初始化函数
 * ============================================================================*/
/**
 * @brief  人脸检测服务初始化
 * @details 初始化结构体 → 创建锁 → 初始化AI模型 → 订阅事件总线
 * @retval 0:成功 -1:失败
 */
static int face_srv_init(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    int ret = -1;

    // 清空控制块
    memset(srv, 0, sizeof(face_detect_srv_t));

    // 初始化状态
    srv->evt_sub_id = -1;
    srv->is_paused = false;
    srv->is_started = false;

    // 初始化互斥锁
    ret = pthread_mutex_init(&srv->lock, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG " 互斥锁初始化失败");
        return -1;
    }

    // 初始化AI模型（使用全局最优配置）
    ai_model_config_t ai_cfg = {
        .model_path    = AI_MODEL_PATH,
        .input_width   = AI_INPUT_WIDTH,
        .input_height  = AI_INPUT_HEIGHT,
        .score_thresh  = AI_SCORE_THRESH,
        .iou_thresh    = AI_IOU_THRESH
    };
    // ====================== 新增调试日志 ======================
    printf("[FACE_SRV_DEBUG] 准备创建AI模型 | 路径=%s | 输入尺寸=%dx%d\n",
           AI_MODEL_PATH, AI_INPUT_WIDTH, AI_INPUT_HEIGHT);

    srv->ai_model = ai_model_mnn_create(&ai_cfg);
    printf("[FACE_SRV_DEBUG] ai_model_mnn_create 返回=%p\n", srv->ai_model);

    if (!srv->ai_model)
    {
        LOG_E(MODULE_TAG " AI模型初始化失败");
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }
    // ====================== 【修复】必须添加这一行！======================
    ret = ai_model_init(srv->ai_model);
    if (ret != AI_MODEL_OK) {
        LOG_E(MODULE_TAG " AI模型初始化执行失败！");
        return -1;
    }
    printf("[FACE_SRV_DEBUG] AI模型初始化成功！\n");
    // 订阅系统事件总线
    event_subscriber_t evt_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = event_bus_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_sub_id = event_bus_subscribe(SYS_EVENT_BUS, &evt_sub);
    if (srv->evt_sub_id < 0)
    {
        LOG_E(MODULE_TAG " 系统事件总线订阅失败");
        ai_model_destroy(srv->ai_model);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    LOG_I(MODULE_TAG " 服务初始化完成，最新FrameLink适配成功");
    return 0;
}

/* =============================================================================
 * 模块自动初始化（系统启动自动调用）
 * ============================================================================*/
/**
 * @brief  模块自动初始化入口
 * @retval 0:成功 -1:失败
 */
static int face_srv_auto_init(void)
{
    if (face_srv_init() != 0)
    {
        return -1;
    }
    LOG_I(MODULE_TAG " 自动加载完成，等待系统启动指令");
    return 0;
}
// 注册为【服务级，优先级3】
MODULE_INIT_LEVEL(INIT_SERVICE, face_srv_auto_init); 
/******************************* End of file **********************************/