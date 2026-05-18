/* SPDX-License-Identifier: MIT */
/**
  ******************************************************************************
  * @file           face_detect_srv.c
  * @brief          人脸检测服务模块（AI专属数据链路版）
  * @details        1. 新增AI专属RGB FrameLink链路，物理隔离主摄像头链路
  *                 2. 适配最新MNN AI接口，外部BGR缓存杜绝内存踩踏
  *                 3. 完整链路生命周期：创建/使用/销毁/安全退出
  *                 4. 严格遵循FrameLink原子引用计数规范
  * @author         Luo
  * @date           2026
  ******************************************************************************
  */

// ==========================================================================
// 【文件私有化宏定义】
// ==========================================================================
#define MODULE_NAME               "FACE_DETECT"
#define MODULE_TAG                "[FACE_DETECT]"

/* 系统总线/链路名称 */
#define SYS_EVENT_BUS             SYS_EVENT_BUS_NAME
#define VIDEO_DATA_BUS            "video"
#define FRAME_LINK_NAME           "main_cam"           // 主摄像头YUYV链路
#define AI_FRAME_LINK_NAME        "ai_rgb"             // AI专属RGB链路（新增）
// AI专属RGB链路配置（添加到你的宏定义区）
#define AI_RGB_POOL_SIZE          FRAME_LINK_POOL_MIN
#define AI_RGB_QUEUE_SIZE         4
/* AI模型配置 */
#define AI_MODEL_PATH             CONFIG_AI_MODEL_PATH
#define AI_INPUT_WIDTH            CONFIG_AI_INPUT_W
#define AI_INPUT_HEIGHT           CONFIG_AI_INPUT_H
#define AI_SCORE_THRESH           CONFIG_AI_SCORE_THRESH
#define AI_IOU_THRESH             CONFIG_AI_IOU_THRESH
#define MAX_FACES                 MAX_FACES

/* 摄像头参数 */
#define CAPTURE_WIDTH             CONFIG_CAPTURE_WIDTH
#define CAPTURE_HEIGHT            CONFIG_CAPTURE_HEIGHT

/* 服务配置 */
#define FRAME_WAIT_TIMEOUT_US     20000
#define AI_PROCESS_QUEUE_SIZE     8
// AI专属RGB帧配置：640x360x3字节
#define AI_RGB_FRAME_SIZE         (CAPTURE_WIDTH * CAPTURE_HEIGHT * 3)

// ==========================================================================
// 头文件包含
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
 * 私有类型定义（扩展AI专属链路句柄）
 * ============================================================================*/
typedef struct {
    // 8字节 指针/句柄 放最前
    ai_model_handle_t            *ai_model;       // AI模型句柄    8/4B
    frame_link_handle_t           ai_rgb_link;     // RGB链路句柄    8/4B
    data_bus_subscription_handle_t data_sub;       // 数据总线订阅   8/4B

    // 8字节 线程/锁
    pthread_t                     work_thread;     // 工作线程      8B
    pthread_mutex_t               lock;            // 线程互斥锁    8/4B

    // 8字节 数组
    FaceInfo_C                    faces[MAX_FACES];// 人脸信息数组

    // 8字节 帧队列
    frame_handle_t                frame_queue[AI_PROCESS_QUEUE_SIZE]; // 帧队列

    // 4字节 数值
    uint32_t                      queue_front;     // 队列头指针    4B
    uint32_t                      queue_rear;      // 队列尾指针    4B
    uint32_t                      queue_count;     // 队列计数      4B
    int                           face_num;        // 检测到人脸数  4B
    int                           evt_sub_id;      // 事件订阅ID    4B

    // 1字节 布尔（紧凑放最后，浪费最小）
    bool                          thread_running;  // 线程运行标志  1B
    bool                          is_paused;       // 暂停标志      1B
    bool                          is_started;      // 启动标志      1B
} face_detect_srv_t;

/* =============================================================================
 * 全局静态单例
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
static inline void face_srv_lock(void)
{
    pthread_mutex_lock(&s_face_srv.lock);
}

static inline void face_srv_unlock(void)
{
    pthread_mutex_unlock(&s_face_srv.lock);
}

/* =============================================================================
 * AI帧队列操作
 * ============================================================================*/
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
 * 数据总线回调
 * ============================================================================*/
static void data_bus_frame_cb(data_bus_item_handle_t item, void *user_data)
{
    (void)user_data;
    face_detect_srv_t *srv = &s_face_srv;

    if (!srv->thread_running || srv->is_paused)
    {
        data_bus_release(item);
        return;
    }

    const void *bus_ptr = data_bus_get_readonly_ptr(item);
    if (!bus_ptr)
    {
        data_bus_release(item);
        return;
    }

    frame_handle_t bus_frame = *(frame_handle_t *)bus_ptr;
    frame_handle_t proc_frame = NULL;

    if (frame_link_consumer_get_by_bus(FRAME_LINK_NAME, bus_frame, &proc_frame) != FL_OK)
    {
        data_bus_release(item);
        return;
    }

    if (!face_queue_enqueue(proc_frame))
    {
        frame_link_put(proc_frame);  // 🔥 替换新接口
    }

    data_bus_release(item);
}

/* =============================================================================
 * 系统事件回调
 * ============================================================================*/
static void event_bus_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    face_detect_srv_t *srv = &s_face_srv;

    switch (event->type)
    {
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG "系统核心就绪");
            break;

        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG "服务暂停");
            srv->is_paused = true;
            break;

        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started)
            {
                face_srv_start();
                srv->is_started = true;
            }
            else
            {
                srv->is_paused = false;
                LOG_I(MODULE_TAG "服务恢复");
            }
            break;

        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
        case EVENT_TYPE_SYS_ERROR:
            face_srv_cleanup();
            break;

        default:
            break;
    }
}

/* =============================================================================
 * AI工作线程（🔥 修复完成版）
 * ============================================================================*/
static void *face_work_thread(void *arg)
{
    (void)arg;
    face_detect_srv_t *srv = &s_face_srv;
    frame_handle_t yuyv_frame = NULL;
    frame_handle_t rgb_frame = NULL;
    frame_info_t frame_info = {0};

    LOG_I(MODULE_TAG "工作线程启动，AI专属RGB链路就绪");

    while (srv->thread_running)
    {
        if (srv->is_paused)
        {
            usleep(FRAME_WAIT_TIMEOUT_US);
            continue;
        }

        // 1. 获取主链路YUYV帧
        yuyv_frame = face_queue_dequeue();
        if (!yuyv_frame)
        {
            usleep(FRAME_WAIT_TIMEOUT_US);
            continue;
        }

        // 2. 从AI专属链路获取空闲RGB帧
        if (frame_link_producer_get(AI_FRAME_LINK_NAME, &rgb_frame) != FL_OK)
        {
            LOG_W(MODULE_TAG "AI链路无空闲帧");
            frame_link_put(yuyv_frame);  // 🔥 替换新接口
            continue;
        }

        // 3. 获取数据指针
        const uint8_t *yuyv_data = frame_get_readonly_ptr(yuyv_frame);
        uint8_t *rgb_data = (uint8_t *)frame_get_writable_ptr(rgb_frame);
        frame_get_info(yuyv_frame, &frame_info);

        // ====================== 🔥 修复2：给AI专属帧设置正确元数据（必加） ======================
        frame_info_t rgb_info = {
            .width = CAPTURE_WIDTH,
            .height = CAPTURE_HEIGHT,
            .format = FRAME_FMT_RGB888,
            .data_size = AI_RGB_FRAME_SIZE,
            .frame_id = frame_info.frame_id,
            .timestamp_us = frame_info.timestamp_us
        };
        frame_set_info(rgb_frame, &rgb_info);

        // 4. AI推理
        srv->face_num = 0;
        memset(srv->faces, 0, sizeof(srv->faces));

        int ret = ai_model_mnn_infer_yuyv(
            yuyv_data,
            CAPTURE_WIDTH,
            CAPTURE_HEIGHT,
            rgb_data,
            srv->faces,
            MAX_FACES,
            &srv->face_num
        );

        // ====================== 🔥 修复3：增加失败日志 ======================
        if (ret != MNN_FACE_OK)
        {
            LOG_E(MODULE_TAG "帧[%u] AI推理失败，错误码:%d", frame_info.frame_id, ret);
            frame_link_producer_push(AI_FRAME_LINK_NAME, rgb_frame);
            frame_link_put(rgb_frame);    // 🔥 必加：释放AI帧
            rgb_frame = NULL;
            frame_link_put(yuyv_frame);  // 🔥 替换新接口
            yuyv_frame = NULL;
            continue;
        }

        // 5. 坐标映射
        if (srv->face_num > 0)
        {
            for (int i = 0; i < srv->face_num; i++)
            {
                ai_model_mnn_map_face(&srv->faces[i], CAPTURE_WIDTH, CAPTURE_HEIGHT);
            }
            LOG_I(MODULE_TAG "帧[%u]检测到%d张人脸", frame_info.frame_id, srv->face_num);
        }
        else
        {
            LOG_I(MODULE_TAG "帧[%u]未检测到人脸", frame_info.frame_id);
        }

        // 6. 释放帧（严格规范）
        frame_link_producer_push(AI_FRAME_LINK_NAME, rgb_frame);
        frame_link_put(rgb_frame);    // 🔥 核心修复：释放AI帧（解决内存池满）
        rgb_frame = NULL;
        
        frame_link_put(yuyv_frame);  // 🔥 替换新接口
        yuyv_frame = NULL;

        // 7. 发布事件
        event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_PROCESS_DONE, MODULE_NAME);
    }

    LOG_I(MODULE_TAG "工作线程安全退出");
    return NULL;
}

/* =============================================================================
 * 服务启动
 * ============================================================================*/
static int face_srv_start(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    int ret = -1;

    // 订阅视频总线
    ret = data_bus_subscribe(VIDEO_DATA_BUS, DATA_TYPE_VIDEO_FRAME, data_bus_frame_cb, NULL, &srv->data_sub);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "数据总线订阅失败");
        return ret;
    }

    // 创建工作线程
    srv->thread_running = true;
    srv->is_paused = false;
    ret = pthread_create(&srv->work_thread, NULL, face_work_thread, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "线程创建失败");
        data_bus_unsubscribe(VIDEO_DATA_BUS, &srv->data_sub);
        srv->thread_running = false;
        return -1;
    }

    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_READY, MODULE_NAME);
    LOG_I(MODULE_TAG "服务启动成功(AI专属RGB链路已启用)");
    return 0;
}

/* =============================================================================
 * 🔥 服务全量清理（销毁AI专属链路）
 * ============================================================================*/
static void face_srv_cleanup(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    frame_handle_t frame = NULL;

    LOG_W(MODULE_TAG "开始安全释放所有资源(含AI专属链路)");

    // 1. 停止线程
    srv->thread_running = false;
    srv->is_paused = true;
    if (srv->work_thread > 0)
    {
        pthread_join(srv->work_thread, NULL);
    }

    // 2. 清空队列
    while ((frame = face_queue_dequeue()) != NULL)
    {
        frame_link_put(frame);  // 🔥 替换新接口
    }

    // 3. 取消订阅
    if (srv->evt_sub_id >= 0)
    {
        event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_sub_id);
    }
    data_bus_unsubscribe(VIDEO_DATA_BUS, &srv->data_sub);

    // 4. 🔥 销毁AI专属RGB链路
    frame_link_destroy(AI_FRAME_LINK_NAME);

    // 5. 销毁AI模型
    if (srv->ai_model)
    {
        ai_model_destroy(srv->ai_model);
        srv->ai_model = NULL;
    }

    // 6. 释放锁
    pthread_mutex_destroy(&srv->lock);

    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_FACE_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG "所有资源释放完成(AI链路已销毁)");
}

/* =============================================================================
 * 🔥 服务初始化（创建AI专属链路）
 * ============================================================================*/
static int face_srv_init(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    int ret = -1;

    // 清空控制块
    memset(srv, 0, sizeof(face_detect_srv_t));
    srv->evt_sub_id = -1;

    // 初始化互斥锁
    ret = pthread_mutex_init(&srv->lock, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "互斥锁初始化失败");
        return -1;
    }
    frame_link_cfg_t ai_link_cfg = {0};  // 统一清空结构体（和采集服务一致）
    strcpy(ai_link_cfg.name, AI_FRAME_LINK_NAME);  // 标准字符串拷贝
    ai_link_cfg.max_frame_size = AI_RGB_FRAME_SIZE;
    ai_link_cfg.pool_count     = AI_RGB_POOL_SIZE;        // 串行处理，仅需1帧
    ai_link_cfg.queue_count    = AI_RGB_QUEUE_SIZE;
    ret = frame_link_create(&ai_link_cfg);
    if (ret != FL_OK)
    {
        LOG_E(MODULE_TAG "AI专属RGB链路创建失败，错误码:%d", ret);
        LOG_E(MODULE_TAG "AI专属RGB链路创建失败");
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 2. 初始化AI模型
    ai_model_config_t ai_cfg = {
        .model_path    = AI_MODEL_PATH,
        .input_width   = AI_INPUT_WIDTH,
        .input_height  = AI_INPUT_HEIGHT,
        .score_thresh  = AI_SCORE_THRESH,
        .iou_thresh    = AI_IOU_THRESH
    };

    srv->ai_model = ai_model_mnn_create(&ai_cfg);
    if (!srv->ai_model)
    {
        LOG_E(MODULE_TAG "AI模型创建失败");
        frame_link_destroy(AI_FRAME_LINK_NAME);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    ret = ai_model_init(srv->ai_model);
    if (ret != AI_MODEL_OK)
    {
        LOG_E(MODULE_TAG "AI模型初始化失败");
        ai_model_destroy(srv->ai_model);
        frame_link_destroy(AI_FRAME_LINK_NAME);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    // 3. 订阅事件总线
    event_subscriber_t evt_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = event_bus_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_sub_id = event_bus_subscribe(SYS_EVENT_BUS, &evt_sub);
    if (srv->evt_sub_id < 0)
    {
        LOG_E(MODULE_TAG "事件总线订阅失败");
        ai_model_destroy(srv->ai_model);
        frame_link_destroy(AI_FRAME_LINK_NAME);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    LOG_I(MODULE_TAG "服务初始化完成(AI专属RGB链路创建成功)");
    return 0;
}

/* =============================================================================
 * 模块自动初始化
 * ============================================================================*/
static int face_srv_auto_init(void)
{
    if (face_srv_init() != 0)
    {
        return -1;
    }
    LOG_I(MODULE_TAG "face_srv_auto_init 自动加载完成，等待系统启动指令");
    return 0;
}

MODULE_INIT_LEVEL(INIT_SERVICE, face_srv_auto_init);
/******************************* End of file **********************************/