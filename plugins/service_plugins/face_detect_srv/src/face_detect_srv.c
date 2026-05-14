/* SPDX-License-Identifier: MIT */
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "vision_ai_config.h"
#include "ai_model_mnn.hpp"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// ==========================================================================
// 全局宏定义（文件私有化，标注来源，方便代码巡查）
// 来源：common\configs\vision_ai_config.h
// ==========================================================================
#define MODULE_NAME           "FACE_DETECT"
#define MODULE_TAG            "[FACE_DETECT]"

// 系统总线名称（全系统统一约定）
#define SYS_EVENT_BUS_NAME    "sys_event"    // 系统事件总线
#define VIDEO_DATA_BUS_NAME   "video"        // 【核心】订阅采集服务的video数据总线

// 服务私有配置
#define FACE_FRAME_WAIT_US    20000   // 20ms 帧等待
#define MAX_FACES             10      // 最大人脸数

// ==========================================================================
// 人脸检测服务 私有结构体（静态单例，外部完全不可见）
// 完整生命周期状态管理 + 资源管理
// ==========================================================================
typedef struct {
    // AI模型句柄
    ai_model_handle_t    *ai_model;
    FaceInfo_C            faces[MAX_FACES];
    int                   face_num;

    // 线程与状态管理
    pthread_t             work_thread;
    bool                  thread_running;
    bool                  is_paused;        // 暂停状态（系统事件控制）
    bool                  is_processing;   // 处理中标记：防止并发处理帧
    pthread_mutex_t       lock;             // 仅保护自身私有变量

    // 零拷贝数据缓存（仅保存总线句柄）
    data_bus_item_handle_t cached_frame;

    // 总线订阅句柄
    int                   evt_sub_id;     // 事件总线订阅ID
    data_bus_subscription_handle_t data_sub; // 数据总线订阅句柄
} face_detect_t;

// 静态单例（服务完全自治，无对外暴露接口）
static face_detect_t s_face_detect;

// ==========================================================================
// 内部工具函数：线程安全加锁/解锁
// ==========================================================================
static inline void _face_lock(void) {
    pthread_mutex_lock(&s_face_detect.lock);
}

static inline void _face_unlock(void) {
    pthread_mutex_unlock(&s_face_detect.lock);
}

// ==========================================================================
// 【核心】服务统一清理函数：释放所有创建的资源（成对释放，无内存泄漏）
// 遵循：谁创建、谁释放 原则
// ==========================================================================
static void face_detect_cleanup(void)
{
    face_detect_t *srv = &s_face_detect;

    LOG_W(MODULE_TAG " 开始执行全量资源释放...");

    // 1. 停止工作线程（最优先，防止线程访问已释放资源）
    srv->thread_running = false;
    srv->is_paused = true;
    if (srv->work_thread > 0) {
        pthread_join(srv->work_thread, NULL);
        LOG_I(MODULE_TAG " 工作线程已安全退出");
        srv->work_thread = 0;
    }

    // 2. 取消事件总线订阅
    if (srv->evt_sub_id >= 0) {
        event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->evt_sub_id);
        srv->evt_sub_id = -1;
        LOG_I(MODULE_TAG " 事件订阅已取消");
    }

    // 3. 取消数据总线订阅（video总线）
    data_bus_unsubscribe(VIDEO_DATA_BUS_NAME, &srv->data_sub);
    LOG_I(MODULE_TAG " 数据总线订阅已取消");

    // 4. 释放缓存的视频帧（零拷贝引用计数归还）
    _face_lock();
    if (srv->cached_frame) {
        data_bus_release(srv->cached_frame);
        srv->cached_frame = NULL;
        LOG_I(MODULE_TAG " 缓存帧已释放");
    }
    _face_unlock();

    // 5. 销毁AI模型
    if (srv->ai_model) {
        ai_model_destroy(srv->ai_model);
        srv->ai_model = NULL;
        LOG_I(MODULE_TAG " AI模型已销毁");
    }

    // 6. 销毁线程互斥锁
    pthread_mutex_destroy(&srv->lock);
    LOG_I(MODULE_TAG " 线程锁已销毁");

    // 7. 发布服务停止事件
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG " 所有资源释放完成，服务已安全退出");
}

// ==========================================================================
// 【核心】数据总线回调：仅做缓存 + 事件通知，零耗时
// 订阅采集服务的 video 总线 RGB 帧
// ==========================================================================
static void _data_bus_frame_cb(data_bus_item_handle_t item, void *user_data)
{
    (void)user_data;
    face_detect_t *srv = &s_face_detect;

    // 服务未运行 / 暂停 / 处理中 → 直接丢弃帧（保护系统）
    if (!srv->thread_running || srv->is_paused || srv->is_processing) {
        data_bus_release(item);
        return;
    }

    // 线程安全：缓存视频帧句柄（零拷贝，仅存指针）
    _face_lock();
    // 释放旧缓存帧，避免内存泄漏
    if (srv->cached_frame) {
        data_bus_release(srv->cached_frame);
    }
    srv->cached_frame = item;
    _face_unlock();

    // 发布事件：通知工作线程开始处理（核心解耦）
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_PROCESS_START, MODULE_NAME);
}

// ==========================================================================
// 事件总线回调：响应系统级指令 + 自动管理生命周期
// 完全响应 main 线程发布的所有系统事件
// ==========================================================================
static void _event_bus_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    face_detect_t *srv = &s_face_detect;

    switch (event->type) {
        // 系统核心就绪
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG " 收到系统核心就绪事件，服务正常运行");
            break;

        // 系统暂停
        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG " 收到系统暂停指令，停止处理帧");
            srv->is_paused = true;
            break;

        // 系统恢复
        case EVENT_TYPE_SYS_RESUME:
            LOG_I(MODULE_TAG " 收到系统恢复指令，恢复帧处理");
            srv->is_paused = false;
            break;

        // 系统正常停止/关机 → 安全清理资源
        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            LOG_I(MODULE_TAG " 收到系统关机/停止指令，执行资源清理");
            face_detect_cleanup();
            break;

        // 系统致命错误 → 强制立即清理所有资源
        case EVENT_TYPE_SYS_ERROR:
            LOG_E(MODULE_TAG " 收到系统致命错误指令，强制清理所有资源！");
            face_detect_cleanup();
            break;

        default:
            break;
    }
}

// ==========================================================================
// 【工作线程】AI推理核心（唯一耗时逻辑，不阻塞总线）
// 支持暂停/停止，线程安全，零拷贝处理
// ==========================================================================
static void *face_work_thread(void *arg)
{
    (void)arg;
    face_detect_t *srv = &s_face_detect;
    LOG_I(MODULE_TAG " 工作线程启动，等待视频帧...");

    while (srv->thread_running) {
        // 暂停/无缓存帧 → 低功耗等待
        if (srv->is_paused || !srv->cached_frame) {
            usleep(FACE_FRAME_WAIT_US);
            continue;
        }

        // ====================== 开始处理帧 ======================
        _face_lock();
        srv->is_processing = true;
        data_bus_item_handle_t frame = srv->cached_frame;
        srv->cached_frame = NULL;
        _face_unlock();

        // 获取帧数据（总线零拷贝，只读访问）
        const uint8_t *rgb_data = data_bus_get_readonly_ptr(frame);
        srv->face_num = 0;

        // AI人脸推理（核心耗时操作）
        ai_model_mnn_infer_yuyv(rgb_data,
                               CONFIG_CAPTURE_WIDTH,
                               CONFIG_CAPTURE_HEIGHT,
                               srv->faces,
                               MAX_FACES,
                               &srv->face_num);

        // 释放帧（归还总线引用计数）
        data_bus_release(frame);

        // ====================== 发布AI检测结果 ======================
        if (srv->face_num > 0) {
            LOG_D(MODULE_TAG " 检测到 %d 张人脸", srv->face_num);
            // 此处可扩展发布AI结果到数据总线
        }

        // 发布事件：推理完成
        event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_PROCESS_DONE, MODULE_NAME);

        // 解锁处理状态
        _face_lock();
        srv->is_processing = false;
        _face_unlock();
    }

    LOG_I(MODULE_TAG " 工作线程退出");
    return NULL;
}

// ==========================================================================
// 服务初始化（自动调用，无对外接口）
// ==========================================================================
static int face_detect_init(void)
{
    face_detect_t *srv = &s_face_detect;
    memset(srv, 0, sizeof(face_detect_t));

    // 初始化状态
    srv->is_paused = false;
    srv->is_processing = false;
    srv->evt_sub_id = -1;

    // 初始化线程互斥锁
    pthread_mutex_init(&srv->lock, NULL);

    // 初始化AI模型（加载全局配置）
    ai_model_config_t ai_cfg = {
        .model_path = CONFIG_AI_MODEL_PATH,
        .input_width = CONFIG_AI_INPUT_W,
        .input_height = CONFIG_AI_INPUT_H,
        .score_thresh = CONFIG_AI_SCORE_THRESH,
        .iou_thresh = CONFIG_AI_IOU_THRESH
    };
    srv->ai_model = ai_model_mnn_create(&ai_cfg);
    if (!srv->ai_model) {
        LOG_E(MODULE_TAG " AI模型初始化失败");
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    LOG_I(MODULE_TAG " 服务初始化完成");
    return 0;
}

// ==========================================================================
// 服务启动（自动调用）
// ==========================================================================
static int face_detect_start(void)
{
    face_detect_t *srv = &s_face_detect;

    // 1. 订阅系统全局事件
    event_subscriber_t evt_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = _event_bus_cb,
        .user_data = srv
    };
    srv->evt_sub_id = event_bus_subscribe(SYS_EVENT_BUS_NAME, &evt_sub);
    if (srv->evt_sub_id < 0) {
        LOG_E(MODULE_TAG " 订阅事件总线失败");
        return -1;
    }

    // 2. 【核心】订阅采集服务的 video 数据总线 RGB 帧
    if (data_bus_subscribe(VIDEO_DATA_BUS_NAME,
                           DATA_TYPE_VIDEO_FRAME_RGB,
                           _data_bus_frame_cb,
                           NULL,
                           &srv->data_sub) != 0)
    {
        LOG_E(MODULE_TAG " 订阅 video 数据总线失败");
        event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->evt_sub_id);
        return -1;
    }

    // 3. 启动工作线程
    srv->thread_running = true;
    if (pthread_create(&srv->work_thread, NULL, face_work_thread, NULL) != 0) {
        LOG_E(MODULE_TAG " 创建工作线程失败");
        data_bus_unsubscribe(VIDEO_DATA_BUS_NAME, &srv->data_sub);
        event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->evt_sub_id);
        srv->thread_running = false;
        return -1;
    }

    // 发布服务状态事件
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_READY, MODULE_NAME);
    event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_RUNNING, MODULE_NAME);
    LOG_I(MODULE_TAG " 服务启动成功，等待视频帧...");

    return 0;
}

// ==========================================================================
// 【内核式】自动初始化 + 启动（无对外接口，main自动加载）
// ==========================================================================
#include "initcall.h"
static int _face_detect_auto_init(void)
{
    // 1. 初始化服务
    if (face_detect_init() != 0) {
        return -1;
    }

    // 2. 自动启动服务，失败则自动清理
    if (face_detect_start() != 0) {
        face_detect_cleanup();
        return -1;
    }

    LOG_I(MODULE_TAG " 自动加载并启动完成");
    return 0;
}
// 注册到系统初始化段，main函数自动加载
MODULE_INIT(_face_detect_auto_init);