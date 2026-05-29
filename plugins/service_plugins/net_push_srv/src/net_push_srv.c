/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           net_push_srv.c
 * @brief          网络推流服务模块（RTSP H.264 + DataBus V4.0 优先级拉模式）
 * @details        1. 【优先级拉流】优先订阅人脸带框帧，降级原始摄像头总线
 *                 2. 事件唤醒无CPU空耗，自动丢弃旧帧
 *                 3. 系统事件控制启停，对齐全应用层架构
 *                 4. 【优化】基于img_joint封装接口实现YUYV转H.264推流
 *                 5. 严格遵循DataBus引用计数规范
 *                 6. IMX6ULL高性能适配，无冗余逻辑
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
#include "img_joint.h"
#include "thread.h"

// 第三方依赖
#include "rtsp_server.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

// ==========================================================================
// 【文件内部私有化宏】→ ✅ 自动适配全局配置，无硬编码
// ==========================================================================
#define MODULE_NAME               "NET_PUSH"
#define MODULE_TAG                "[NET_PUSH]"

#define NET_PUSH_TARGET_FPS        GLOBAL_VIDEO_FPS
#define FRAME_INTERVAL_MS          GLOBAL_FRAME_INTERVAL_MS

// ✅ 自动适配：帧等待超时 = 2倍帧间隔（随全局FPS自动变化，不再固定30）
#define FRAME_WAIT_TIMEOUT_MS     (FRAME_INTERVAL_MS * 2)
// ✅ 640x360@15fps 最优码率：500kbps（固定值完全够用，无需动态调整）
#define H264_BITRATE              500

#define FACE_RESULT_RGB_DATA_BUS  FACE_YUV_DATA_BUS_NAME
#define VIDEO_DATA_BUS            VIDEO_DATA_BUS_NAME
#define SYS_EVENT_BUS             SYS_EVENT_BUS_NAME

// 🔥 【革命核心】新增 H.264 数据总线配置
#define H264_DATA_BUS_NAME        H264_RTSP_DATA_BUS_NAME
#define H264_MAX_FRAME_SIZE       (1024 * 1024)   // H264单帧最大大小
#define H264_BUS_MAX_ITEMS        10                // 总线缓存8帧（防丢流）
#define H264_BUS_MAX_SUBSCRIBER   1                // 仅RTSP订阅

#define VIDEO_WIDTH               GLOBAL_VIDEO_WIDTH
#define VIDEO_HEIGHT              GLOBAL_VIDEO_HEIGHT
#define H264_GOP                  GLOBAL_VIDEO_FPS

// 线程配置（适配通用线程组件）
#define NET_PUSH_THREAD_STACK_SIZE (1024 * 1024)  // 1MB栈
#define NET_PUSH_RT_PRIORITY       90              // 推流实时优先级(最高)
#define NET_PUSH_CPU_ID            0               // 绑定CPU0

// 帧率优化配置：设置为x = 每x次推流事件处理1次:cap 14fps ---2---7
#define FPS_DOWNSAMPLE_STEP           2
#define TARGET_PUSH_FPS               5

// ==========================================================================
// @brief 网络推流服务控制块
// ============================================================================
typedef struct {
    thread_t                work_thread;
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;
    bool                    is_paused;
    bool                    is_started;

    int                     evt_sys_sub_id;
    int                     evt_capture_sub_id;

    h264_encoder_t          h264_enc;
    // 🔥 删除：静态h264_buf（改用数据总线）

    uint32_t                frame_sample_cnt;

    uint8_t                 sps_pps_cache[256];
    uint32_t                sps_pps_len;
    bool                    rtsp_started;
    bool                    last_rtsp_client_state;
} net_push_srv_t;

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
// ✅ 【新增】H.264 NAL单元完整打印（支持多NAL，遍历所有帧）
// 解决：只打印第一个NAL，看不到PPS/IDR的问题
// ============================================================================
static void net_push_print_h264_nal(const uint8_t* h264_data, int data_len)
{
    if (!h264_data || data_len <= 4) {
        return;
    }

    int pos = 0;
    const int max_pos = data_len - 4;

    while (pos <= max_pos) {
        // 检测H.264标准起始码 (0x00 00 01 或 0x00 00 00 01)
        int start_code_len = 0;
        if (h264_data[pos] == 0x00 && h264_data[pos+1] == 0x00 && h264_data[pos+2] == 0x01) {
            start_code_len = 3;
        } else if (h264_data[pos] == 0x00 && h264_data[pos+1] == 0x00 && h264_data[pos+2] == 0x00 && h264_data[pos+3] == 0x01) {
            start_code_len = 4;
        } else {
            pos++;
            continue;
        }

        // 提取NAL类型
        uint8_t nal_type = h264_data[pos + start_code_len] & 0x1F;
        int nal_size = 0;

        // 计算当前NAL单元大小
        int next_pos = pos + start_code_len;
        while (next_pos <= max_pos) {
            if ((next_pos + 3 <= max_pos && h264_data[next_pos] == 0x00 && h264_data[next_pos+1] == 0x00 && h264_data[next_pos+2] == 0x01) ||
                (next_pos + 4 <= max_pos && h264_data[next_pos] == 0x00 && h264_data[next_pos+1] == 0x00 && h264_data[next_pos+2] == 0x00 && h264_data[next_pos+3] == 0x01)) {
                break;
            }
            next_pos++;
        }
        nal_size = next_pos - pos;

        // 打印调试信息
        printf("[NET_PUSH_DEBUG] NAL: 类型=0x%02X | 大小=%d bytes | 总帧长=%d\n",
               nal_type, nal_size, data_len);

        // 中文标注
        switch(nal_type) {
            case 0x07: printf("[NET_PUSH_DEBUG] ✅ SPS (序列参数集)\n"); break;
            case 0x08: printf("[NET_PUSH_DEBUG] ✅ PPS (图像参数集)\n"); break;
            case 0x05: printf("[NET_PUSH_DEBUG] ✅ IDR (关键帧)\n"); break;
            case 0x01: printf("[NET_PUSH_DEBUG] ✅ P  (普通预测帧)\n"); break;
            default: break;
        }

        pos = next_pos;
    }
    printf("----------------------------------------\n");
}

// ==========================================================================
// 毫秒级条件等待
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
// 事件总线回调
// ============================================================================
static void net_push_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    net_push_srv_t *srv = &s_net_push_srv;

    switch (event->type)
    {
        case EVENT_TYPE_CAPTURE_PROTO_READY:
            if (thread_is_running(&srv->work_thread) && !srv->is_paused)
            {
                pthread_mutex_lock(&srv->mutex);
                pthread_cond_signal(&srv->cond);
                pthread_mutex_unlock(&srv->mutex);
            }
            break;

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

        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG "服务进入暂停状态");
            srv->is_paused = true;
            break;

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
// 推流工作线程（极致低功耗：无客户端完全休眠，有客户端才编码）
// ============================================================================
static void *net_push_work_thread(void *arg)
{
    net_push_srv_t *srv = &s_net_push_srv;
    data_bus_item_handle_t frame_item = NULL;
    data_bus_item_handle_t h264_item = NULL; // 🔥 H264总线项
    const uint8_t *frame_data = NULL;
    size_t frame_size = 0;
    int h264_len = 0;
    uint8_t *h264_wbuf = NULL;

    srv->last_rtsp_client_state = ( rtsp_has_clients() && srv->rtsp_started );
    struct timespec last_ts;
    clock_gettime(CLOCK_MONOTONIC, &last_ts);
    LOG_I(MODULE_TAG "推流工作线程启动成功，等待视频数据...");

    while (thread_is_running(&srv->work_thread))
    {
        if (srv->is_paused) {
            thread_sleep_ms(50);
            continue;
        }

        pthread_mutex_lock(&srv->mutex);
        pthread_cond_timedwait_ms(&srv->cond, &srv->mutex, FRAME_WAIT_TIMEOUT_MS);
        pthread_mutex_unlock(&srv->mutex);

        // 帧率降采样
        srv->frame_sample_cnt++;
        if (srv->frame_sample_cnt < FPS_DOWNSAMPLE_STEP)
        {
            continue;
        }
        srv->frame_sample_cnt = 0;

        // 客户端状态管理
        bool current_client_state = ( rtsp_has_clients() && srv->rtsp_started );
        if (current_client_state != srv->last_rtsp_client_state)
        {
            srv->last_rtsp_client_state = current_client_state;
            if (current_client_state) {
                event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_RTSP_CONNECTED, MODULE_NAME);
                LOG_I(MODULE_TAG "RTSP客户端已连接，暂停人脸抓拍");
            } else {
                event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_RTSP_DISCONNECTED, MODULE_NAME);
                LOG_I(MODULE_TAG "RTSP客户端已断开，恢复人脸抓拍");
            }
        }

        // 🔥 核心：有客户端才编码 + 推送H264总线
        if (current_client_state)
        {
            if (data_bus_pull_latest(VIDEO_DATA_BUS, DATA_TYPE_VIDEO, &frame_item) == DATA_BUS_OK)
            {
                frame_data = data_bus_get_readonly_ptr(frame_item);
                frame_size = data_bus_get_item_size(frame_item);

                if (frame_data && frame_size)
                {
                    // 1. 分配H264总线内存
                    if (data_bus_alloc(H264_DATA_BUS_NAME,
                                       DATA_TYPE_H264,
                                       H264_MAX_FRAME_SIZE,
                                       MODULE_NAME,
                                       &h264_item) == DATA_BUS_OK)
                    {
                        // 2. 获取可写指针
                        h264_wbuf = data_bus_get_writable_ptr(h264_item);
                        h264_len = H264_MAX_FRAME_SIZE;

                        // 3. H264编码（直接写入总线）
                        int ret_h = yuyv_to_h264(srv->h264_enc, frame_data, frame_size, h264_wbuf, &h264_len);
                        if ( ret_h == IMG_JOINT_OK)
                        {

                                data_bus_set_item_size(h264_item, h264_len);
                                // 4. 推送H264总线（零拷贝）
                                data_bus_push(H264_DATA_BUS_NAME, h264_item);
                        }
                        else if(ret_h == IMG_JOINT_ERR_SKIP)
                        {
                            LOG_I(MODULE_TAG "算力不够,正常跳帧");
                        }
                        else
                        {

                            LOG_E(MODULE_TAG "YUYV转H264编码失败");
                        }
                        // 5. 生产者释放总线引用
                        data_bus_release(h264_item);
                        h264_item = NULL;
                    }
                }
                data_bus_release(frame_item);
                frame_item = NULL;
            }
        }
        else
        {
            thread_sleep_ms(FRAME_INTERVAL_MS);
        }
    }

    LOG_I(MODULE_TAG "推流工作线程正常退出");
    return NULL;
}

// ==========================================================================
// 服务启动（🔥 初始化H264数据总线）
// ============================================================================
static int net_push_srv_start(void)
{
    net_push_srv_t *srv = &s_net_push_srv;
    thread_err_t thread_ret;
    int ret = -1;

    // 🔥 第一步：初始化 H.264 数据总线
    data_bus_config_t h264_bus_cfg = {
        .max_item_size = H264_MAX_FRAME_SIZE,
        .max_items = H264_BUS_MAX_ITEMS,
        .max_subscribers = H264_BUS_MAX_SUBSCRIBER,
        .name = H264_DATA_BUS_NAME,
    };
    if (data_bus_init(&h264_bus_cfg) != DATA_BUS_OK) {
        LOG_E(MODULE_TAG "H264数据总线初始化失败");
        return -1;
    }
    LOG_I(MODULE_TAG "✅ H264数据总线初始化成功");

    // 初始化同步变量
    ret = pthread_cond_init(&srv->cond, NULL);
    if (ret != 0) {
        LOG_E(MODULE_TAG "条件变量初始化失败");
        data_bus_deinit(H264_DATA_BUS_NAME);
        return -1;
    }

    // 初始化H264编码器
    h264_encode_param_t enc_param = {
        .width = VIDEO_WIDTH,
        .height = VIDEO_HEIGHT,
        .fps = NET_PUSH_TARGET_FPS/FPS_DOWNSAMPLE_STEP,
        .bitrate = H264_BITRATE,
        .gop = H264_GOP,
    };
    LOG_I(MODULE_TAG "创建H264编码器 | %dx%d | %dFPS | GOP=%d",
          VIDEO_WIDTH, VIDEO_HEIGHT, NET_PUSH_TARGET_FPS, H264_GOP);
    srv->h264_enc = h264_encoder_create(&enc_param);
    if (!srv->h264_enc) {
        LOG_E(MODULE_TAG "H.264编码器创建失败");
        pthread_cond_destroy(&srv->cond);
        data_bus_deinit(H264_DATA_BUS_NAME);
        return -2;
    }

    // 获取SPS/PPS
    srv->sps_pps_len = sizeof(srv->sps_pps_cache);
    if (h264_encoder_get_sps_pps(srv->h264_enc, srv->sps_pps_cache, &srv->sps_pps_len) == IMG_JOINT_OK)
    {
        LOG_I(MODULE_TAG "获取SPS+PPS成功 | 大小: %d bytes", srv->sps_pps_len);
        rtsp_set_sps_pps(srv->sps_pps_cache, srv->sps_pps_len);
        if (rtsp_start_service() == 0) {
            srv->rtsp_started = true;
            LOG_I(MODULE_TAG "✅ RTSP服务启动成功");
        }
    }

    // 创建实时线程
    thread_ret = thread_create_rt(&srv->work_thread,
                                  "NET_Push",
                                  NET_PUSH_THREAD_STACK_SIZE,
                                  net_push_work_thread,
                                  NULL,
                                  NET_PUSH_RT_PRIORITY,
                                  NET_PUSH_CPU_ID);

    if (thread_ret != THREAD_OK) {
        LOG_E(MODULE_TAG "实时推流线程创建失败 err=%d", thread_ret);
        h264_encoder_destroy(srv->h264_enc);
        pthread_cond_destroy(&srv->cond);
        data_bus_deinit(H264_DATA_BUS_NAME);
        return -5;
    }

    srv->is_paused = false;
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_NET_READY, MODULE_NAME);
    LOG_I(MODULE_TAG "推流服务启动完成 [实时优先级=90 | 绑定CPU0]");
    return 0;
}

// ==========================================================================
// 资源清理（🔥 销毁H264总线）
// ============================================================================
static void net_push_srv_cleanup(void)
{
    net_push_srv_t *srv = &s_net_push_srv;

    LOG_W(MODULE_TAG "开始释放所有资源");
    thread_stop(&srv->work_thread);
    srv->is_paused = true;

    pthread_mutex_lock(&srv->mutex);
    pthread_cond_signal(&srv->cond);
    pthread_mutex_unlock(&srv->mutex);

    if (thread_is_running(&srv->work_thread)) {
        thread_join(&srv->work_thread, NULL);
    }

    // 取消订阅
    if (srv->evt_sys_sub_id >= 0) event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_sys_sub_id);
    if (srv->evt_capture_sub_id >= 0) event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_capture_sub_id);

    // 释放编码器
    if (srv->h264_enc) { h264_encoder_destroy(srv->h264_enc); srv->h264_enc = NULL; }

    // 停止RTSP + 销毁H264总线
    rtsp_server_stop();
    data_bus_deinit(H264_DATA_BUS_NAME);
    srv->rtsp_started = false;
    srv->sps_pps_len = 0;

    pthread_cond_destroy(&srv->cond);
    pthread_mutex_destroy(&srv->mutex);

    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_NET_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG "所有资源释放完成");
}

// ==========================================================================
// 服务初始化
// ============================================================================
static int net_push_srv_init(void)
{
    net_push_srv_t *srv = &s_net_push_srv;
    int ret = -1;

    memset(srv, 0, sizeof(net_push_srv_t));
    srv->evt_sys_sub_id = -1;
    srv->evt_capture_sub_id = -1;
    srv->frame_sample_cnt = 0;

    ret = pthread_mutex_init(&srv->mutex, NULL);
    if (ret != 0) {
        LOG_E(MODULE_TAG "互斥锁初始化失败");
        return -1;
    }

    // 订阅系统事件
    event_subscriber_t sys_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = net_push_event_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_sys_sub_id = event_bus_subscribe(SYS_EVENT_BUS, &sys_sub);

    if (srv->evt_sys_sub_id < 0 )
    {
        LOG_E(MODULE_TAG "事件订阅失败");
        net_push_srv_cleanup();
        return -3;
    }

    LOG_I(MODULE_TAG "网络推流服务初始化完成");
    return 0;
}

// ==========================================================================
// 模块自动初始化
// ============================================================================
static int net_push_srv_auto_init(void)
{
    if (net_push_srv_init() != 0) return -1;
    LOG_I(MODULE_TAG "模块自动加载完成，等待系统启动指令");
    return 0;
}

MODULE_INIT_LEVEL(INIT_SERVICE, net_push_srv_auto_init);

/******************************* End of file **********************************/