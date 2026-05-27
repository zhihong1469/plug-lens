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
 *                 6. 集成OpenCV原生绘制人脸框，发布带框RGB帧
 *                 7. 【优化】解码RGB + 画框RGB 双总线物理隔离
 *                 8. 【新增】15fps采集 → 5fpsAI检测，精准帧率控制
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
#define VIDEO_DATA_BUS            VIDEO_DATA_BUS_NAME                // 主摄像头MJPEG/YUYV总线
#define AI_RAW_RGB_DATA_BUS       AI_RGB_DATA_BUS_NAME               // 【独立总线1】AI解码后原始RGB帧
#define FACE_RESULT_RGB_DATA_BUS  FACE_YUV_DATA_BUS_NAME             // 【独立总线2】带人脸框的结果RGB帧
#define CAPTURE_EVENT_BUS         SYS_EVENT_BUS_NAME                 // 采集事件总线

/* 资源配置 - 双RGB总线独立配置 */
#define AI_RAW_RGB_POOL_SIZE          4
#define AI_RAW_RGB_MAX_SUBSCRIBERS    4
#define FACE_RESULT_RGB_POOL_SIZE     4
#define FACE_RESULT_RGB_MAX_SUBSCRIBERS 4
#define AI_MAX_FACES                  MAX_FACES

/* AI模型参数 */
#define AI_MODEL_PATH                 CONFIG_AI_MODEL_PATH
#define AI_INPUT_WIDTH                CONFIG_AI_INPUT_W
#define AI_INPUT_HEIGHT               CONFIG_AI_INPUT_H
#define AI_SCORE_THRESH               CONFIG_AI_SCORE_THRESH
#define AI_IOU_THRESH                 CONFIG_AI_IOU_THRESH

/* 摄像头参数 */
#define CAPTURE_WIDTH                 GLOBAL_VIDEO_WIDTH
#define CAPTURE_HEIGHT                GLOBAL_VIDEO_HEIGHT

/* 线程配置（适配通用线程组件） */
#define FACE_THREAD_STACK_SIZE        (1024 * 1024)  // 1MB栈
#define FACE_RT_PRIORITY              70              // 实时优先级(低于采集80)
#define FACE_CPU_ID                   0               // 绑定CPU0
#define FRAME_WAIT_TIMEOUT_MS         200

/* 帧率优化配置：采集15fps → 检测5fps = 每3帧处理1次 */
#define FPS_DOWNSAMPLE_STEP           30
#define TARGET_AI_FPS                 5

/* 帧大小配置（双RGB帧大小一致，总线物理隔离） */
#define AI_RAW_RGB_FRAME_SIZE         (CAPTURE_WIDTH * CAPTURE_HEIGHT * 3)    // 解码后原始RGB
#define FACE_RESULT_RGB_FRAME_SIZE    (CAPTURE_WIDTH * CAPTURE_HEIGHT * 3)    // 画框后结果RGB

// ==========================================================================
// 头文件包含
// ==========================================================================
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "vision_ai_config.h"
#include "ai_model_mnn.hpp"
#include "initcall.h"
#include "sd_storage.h"
#include "thread.h"   // 新增：引入通用线程组件
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include "led_base.h"
/* =============================================================================
 * @brief 人脸检测服务控制块（线程安全 + 事件唤醒 + 帧率控制）
 * ============================================================================*/
typedef struct {
    ai_model_handle_t            *ai_model;              /* AI模型句柄 */

    /* 线程控制：替换为封装thread_t */
    thread_t                     work_thread;           /* 封装工作线程 */
    pthread_mutex_t               mutex;                 /* 条件变量互斥锁 */
    pthread_cond_t                cond;                  /* 事件唤醒条件变量 */
    bool                          is_paused;             /* 服务暂停标志 */
    bool                          is_started;            /* 服务启动标志 */

    /* 事件订阅 */
    int                           evt_sys_sub_id;        /* 系统事件订阅ID */
    int                           evt_capture_sub_id;    /* 采集事件订阅ID */

    /* AI检测结果 */
    FaceInfo_C                    faces[AI_MAX_FACES];    /* 人脸信息数组 */
    int                           face_num;              /* 检测到人脸数量 */

    /* SD卡存储 */
    SdStorage_t                  *sd_storage;            /* SD卡存储句柄 */

    /* 帧率控制：新增帧计数器 */
    uint32_t                      frame_sample_cnt;       /* 事件采样计数器 */
    led_base_t                    *s_led;

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
            if (thread_is_running(&srv->work_thread) && !srv->is_paused)
            {
                pthread_mutex_lock(&srv->mutex);
                pthread_cond_signal(&srv->cond);  /* 唤醒工作线程 */
                pthread_mutex_unlock(&srv->mutex);
            }
            break;

        // ✅ RTSP有客户端：暂停人脸服务（只执行1次）
        case EVENT_TYPE_RTSP_CONNECTED:
            if (!srv->is_paused) {
                srv->is_paused = true;
                LOG_I(MODULE_TAG "RTSP推流中，暂停人脸检测/抓拍");
            }
            break;

        // ✅ RTSP无客户端：恢复人脸服务（只执行1次）
        case EVENT_TYPE_RTSP_DISCONNECTED:
            if (srv->is_paused) {
                srv->is_paused = false;
                LOG_I(MODULE_TAG "RTSP已断开，恢复人脸检测/抓拍");
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

// 项目封装函数：毫秒级条件等待
int pthread_cond_timedwait_ms(pthread_cond_t *cond,
                              pthread_mutex_t *mutex,
                              uint32_t timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    return pthread_cond_timedwait(cond, mutex, &ts);
}

/* =============================================================================
 * @brief   AI工作线程（拉模式 + 事件唤醒 + OpenCV画框 + 双RGB总线隔离 + 5fps帧率控制）
 * @param   arg: 线程参数
 * @return  线程退出码
 * ============================================================================*/
static void *face_work_thread(void *arg)
{
    (void)arg;
    face_detect_srv_t *srv = &s_face_srv;
    data_bus_item_handle_t camera_item = NULL;      // 原始摄像头MJPEG帧
    data_bus_item_handle_t raw_rgb_item = NULL;     // 【独立1】AI解码后原始RGB帧
    data_bus_item_handle_t result_rgb_item = NULL;  // 【独立2】带人脸框的结果RGB帧
    int ret;

    LOG_I(MODULE_TAG "AI工作线程启动【拉模式+事件唤醒+5fps帧率控制】");

    // 使用封装线程状态判断循环
    while (thread_is_running(&srv->work_thread))
    {
        /* 暂停状态：低功耗等待 */
        if (srv->is_paused)
        {
            thread_sleep_ms(FRAME_WAIT_TIMEOUT_MS);
            continue;
        }

        /* ============== 核心：等待采集事件唤醒 ============== */
        pthread_mutex_lock(&srv->mutex);
        pthread_cond_timedwait_ms(&srv->cond, &srv->mutex, FRAME_WAIT_TIMEOUT_MS);
        pthread_mutex_unlock(&srv->mutex);

        // ====================== 帧率优化核心：每3帧处理1次(15→5fps) ======================
        srv->frame_sample_cnt++;
        if (srv->frame_sample_cnt < FPS_DOWNSAMPLE_STEP)
        {
            continue; // 不处理，直接跳过
        }
        // 重置计数器，执行AI推理
        srv->frame_sample_cnt = 0;

        /* ============== 拉取最新摄像头原始帧 ============== */
        ret = data_bus_pull_latest(VIDEO_DATA_BUS, DATA_TYPE_VIDEO, &camera_item);
        if (ret != DATA_BUS_OK || !camera_item)
        {
            continue;
        }
        const uint8_t *src_camera = data_bus_get_readonly_ptr(camera_item);

        /* ============== 申请【解码后原始RGB帧】（独立总线） ============== */
        ret = data_bus_alloc(AI_RAW_RGB_DATA_BUS,
                             DATA_TYPE_VIDEO_RGB,
                             AI_RAW_RGB_FRAME_SIZE,
                             MODULE_NAME,
                             &raw_rgb_item);
        if (ret != DATA_BUS_OK)
        {
            LOG_W(MODULE_TAG "AI原始RGB总线无空闲帧");
            data_bus_release(camera_item);
            camera_item = NULL;
            continue;
        }
        uint8_t *raw_rgb_data = data_bus_get_writable_ptr(raw_rgb_item);

        /* ============== AI推理：MJPEG解码+RGB转换 + 人脸检测 ============== */
        srv->face_num = 0;
        memset(srv->faces, 0, sizeof(srv->faces));
        ret = ai_model_mnn_infer_image(src_camera,
                                      CAPTURE_WIDTH,
                                      CAPTURE_HEIGHT,
                                      raw_rgb_data,
                                      srv->faces,
                                      AI_MAX_FACES,
                                      &srv->face_num,
                                      INPUT_FORMAT);

        /* 推理失败：直接释放资源 */
        if (ret != MNN_FACE_OK)
        {
            LOG_E(MODULE_TAG "AI推理失败，错误码:%d", ret);
            goto release_res;
        }

        /* ============== 无人脸：直接释放，不发布结果帧 ============== */
        if (srv->face_num <= 0)
        {
            LOG_D(MODULE_TAG "未检测到人脸");
            goto release_res;
        }

        LOG_I(MODULE_TAG "检测到 %d 张人脸", srv->face_num);
        led_base_turn_on(srv->s_led);
        /* ============== 绘制人脸框 ============== */
        ret = data_bus_alloc(FACE_RESULT_RGB_DATA_BUS,
                             DATA_TYPE_VIDEO_RGB,
                             FACE_RESULT_RGB_FRAME_SIZE,
                             MODULE_NAME,
                             &result_rgb_item);
        if (ret == DATA_BUS_OK)
        {
            uint8_t *result_rgb_data = data_bus_get_writable_ptr(result_rgb_item);
            ai_model_mnn_map_and_draw_faces(srv->faces,
                                            srv->face_num,
                                            CAPTURE_WIDTH,
                                            CAPTURE_HEIGHT,
                                            raw_rgb_data,
                                            result_rgb_data);
            LOG_D(MODULE_TAG "人脸框绘制完成");
            
            // SD卡保存
            if (srv->sd_storage) 
            {
                if(SdStorage_SaveJpeg(srv->sd_storage, result_rgb_data) == SD_STORAGE_OK)
                {
                    led_base_turn_off(srv->s_led);
                }
                else
                {
                    LOG_E(MODULE_TAG "SD卡保存失败");
                }
            }
        }
        else
        {
            // LOG_W(MODULE_TAG "人脸结果RGB总线无空闲帧，跳过画框");

        }

release_res:
        /* 释放资源 */
        if (result_rgb_item)  { data_bus_release(result_rgb_item); }
        if (raw_rgb_item)     { data_bus_release(raw_rgb_item); }
        if (camera_item)      { data_bus_release(camera_item); }
        
        raw_rgb_item = camera_item = result_rgb_item = NULL;

        /* 发布AI处理完成事件 */
        event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_PROCESS_DONE, MODULE_NAME);
    }

    LOG_I(MODULE_TAG "AI工作线程安全退出");
    return NULL;
}

/* =============================================================================
 * @brief   服务启动函数（简化版：一键实时线程，代码精简70%）
 * @return  0:成功 负数:失败
 * ============================================================================*/
static int face_srv_start(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    thread_err_t thread_ret;

    /* 初始化条件变量 */
    int ret = pthread_cond_init(&srv->cond, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "条件变量初始化失败");
        return -1;
    }

    // 一键创建实时线程：命名+栈大小+入口+优先级+CPU绑定 一步到位
    thread_ret = thread_create_rt(&srv->work_thread,
                                  "FACE_Detect",
                                  FACE_THREAD_STACK_SIZE,
                                  face_work_thread,
                                  NULL,
                                  FACE_RT_PRIORITY,
                                  FACE_CPU_ID);

    if (thread_ret != THREAD_OK)
    {
        LOG_E(MODULE_TAG "实时工作线程创建失败 err=%d", thread_ret);
        pthread_cond_destroy(&srv->cond);
        return -1;
    }

    srv->is_paused = false;
    // event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_READY, MODULE_NAME);
    LOG_I(MODULE_TAG "人脸检测服务启动成功 [实时优先级=70 | 5fps]");
    return 0;
}

/* =============================================================================
 * @brief   服务资源清理（适配封装线程）
 * ============================================================================*/
static void face_srv_cleanup(void)
{
    face_detect_srv_t *srv = &s_face_srv;

    LOG_W(MODULE_TAG "开始释放所有资源");

    // 1. 安全停止线程（封装API）
    thread_stop(&srv->work_thread);
    srv->is_paused = true;

    /* 唤醒阻塞线程 */
    pthread_mutex_lock(&srv->mutex);
    pthread_cond_signal(&srv->cond);
    pthread_mutex_unlock(&srv->mutex);

    // 2. 等待线程退出（封装API）
    if (thread_is_running(&srv->work_thread))
    {
        thread_join(&srv->work_thread, NULL);
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

    /* 销毁同步变量 */
    pthread_cond_destroy(&srv->cond);
    pthread_mutex_destroy(&srv->mutex);

    /* 销毁AI模型 + 双RGB总线 */
    if (srv->ai_model)
    {
        ai_model_destroy(srv->ai_model);
        srv->ai_model = NULL;
    }
    data_bus_deinit(AI_RAW_RGB_DATA_BUS);
    data_bus_deinit(FACE_RESULT_RGB_DATA_BUS);

    /* 释放SD卡 */
    if (srv->sd_storage) {
        SdStorage_Deinit(srv->sd_storage);
        srv->sd_storage = NULL;
    }

    led_base_turn_off(srv->s_led);
    led_indicator_destroy(srv->s_led);

    // event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_STOPPED, MODULE_NAME);
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
    srv->frame_sample_cnt = 0;  // 帧率计数器初始化

    /* 初始化互斥锁 */
    ret = pthread_mutex_init(&srv->mutex, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "互斥锁初始化失败");
        return -1;
    }

    /* 初始化AI原始RGB总线 */
    data_bus_config_t ai_raw_rgb_cfg = {
        .name = AI_RAW_RGB_DATA_BUS,
        .max_item_size = AI_RAW_RGB_FRAME_SIZE,
        .max_items = AI_RAW_RGB_POOL_SIZE,
        .max_subscribers = AI_RAW_RGB_MAX_SUBSCRIBERS
    };
    ret = data_bus_init(&ai_raw_rgb_cfg);
    if (ret != DATA_BUS_OK)
    {
        LOG_E(MODULE_TAG "AI原始RGB总线初始化失败");
        pthread_mutex_destroy(&srv->mutex);
        return -1;
    }

    /* 初始化人脸结果RGB总线 */
    data_bus_config_t face_result_rgb_cfg = {
        .name = FACE_RESULT_RGB_DATA_BUS,
        .max_item_size = FACE_RESULT_RGB_FRAME_SIZE,
        .max_items = FACE_RESULT_RGB_POOL_SIZE,
        .max_subscribers = FACE_RESULT_RGB_MAX_SUBSCRIBERS
    };
    ret = data_bus_init(&face_result_rgb_cfg);
    if (ret != DATA_BUS_OK)
    {
        LOG_E(MODULE_TAG "人脸结果RGB总线初始化失败");
        pthread_mutex_destroy(&srv->mutex);
        data_bus_deinit(AI_RAW_RGB_DATA_BUS);
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
        data_bus_deinit(AI_RAW_RGB_DATA_BUS);
        data_bus_deinit(FACE_RESULT_RGB_DATA_BUS);
        pthread_mutex_destroy(&srv->mutex);
        return -1;
    }

    /* 订阅事件总线 */
    event_subscriber_t sys_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = event_bus_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_sys_sub_id = event_bus_subscribe(SYS_EVENT_BUS_NAME, &sys_sub);

    if (srv->evt_sys_sub_id < 0 ) 
    {
        LOG_E(MODULE_TAG "事件总线订阅失败");
        face_srv_cleanup();
        return -1;
    }

    /* 初始化SD卡 */
    srv->sd_storage = SdStorage_Init();
    if (srv->sd_storage) {
        LOG_I(MODULE_TAG "SD卡存储初始化成功");
    } else {
        LOG_W(MODULE_TAG "SD卡存储初始化失败，将无法保存人脸图片");
    }
    // 创建LED实例
    srv->s_led = led_indicator_create("/dev/100ask_led0");
    if (srv->s_led) {
        led_base_init(srv->s_led);
    }
    LOG_I(MODULE_TAG "人脸检测服务初始化完成（5fps+双RGB总线隔离）");
    return 0;
}

/* =============================================================================
 * @brief   模块自动初始化
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