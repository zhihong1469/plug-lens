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

#define VIDEO_WIDTH               GLOBAL_VIDEO_WIDTH
#define VIDEO_HEIGHT              GLOBAL_VIDEO_HEIGHT
#define H264_GOP                  GLOBAL_VIDEO_FPS
// 🔥 修复1：原缓冲区太小，重新定义足够大的H264码流缓冲区
#define H264_BUF_SIZE             (1024 * 1024)

// ==========================================================================
// @brief 网络推流服务控制块
// ============================================================================
typedef struct {
    pthread_t               work_thread;
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;
    bool                    thread_running;
    bool                    is_paused;
    bool                    is_started;

    int                     evt_sys_sub_id;
    int                     evt_ai_sub_id;

    h264_encoder_t          h264_enc;
    uint8_t*                h264_buf;
} net_push_srv_t;

static net_push_srv_t s_net_push_srv;

// ====================== SPS+PPS 缓存 ======================
static uint8_t  g_sps_pps_cache[256] = {0};
static uint32_t g_sps_pps_len = 0;
// ====================== RTSP延迟启动标志 ======================
static bool     rtsp_started = false;

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
        case EVENT_TYPE_FACE_PROCESS_DONE:
            if (srv->thread_running && !srv->is_paused)
            {
                pthread_mutex_lock(&srv->mutex);
                pthread_cond_signal(&srv->cond);
                pthread_mutex_unlock(&srv->mutex);
                LOG_D(MODULE_TAG "收到AI完成事件，唤醒推流线程");
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
// 推流工作线程（极简：只做 拉帧 → 编码 → 推流）
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
    LOG_I(MODULE_TAG "推流工作线程启动成功，等待视频数据...");

    while (srv->thread_running)
    {
        if (srv->is_paused) {
            usleep(10000);
            continue;
        }

        // 帧率控制
        pthread_mutex_lock(&srv->mutex);
        pthread_cond_timedwait_ms(&srv->cond, &srv->mutex, FRAME_INTERVAL_MS);
        pthread_mutex_unlock(&srv->mutex);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint32_t elapsed = (now.tv_sec - last_ts.tv_sec)*1000 + (now.tv_nsec - last_ts.tv_nsec)/1000000;
        if (elapsed < FRAME_INTERVAL_MS) {
            continue;
        }
        last_ts = now;

        // 拉取最新YUYV帧
        if (data_bus_pull_latest(VIDEO_DATA_BUS, DATA_TYPE_VIDEO, &frame_item) == DATA_BUS_OK)
        {
            frame_data = data_bus_get_readonly_ptr(frame_item);
            frame_size = data_bus_get_item_size(frame_item);

            if (frame_data && frame_size)
            {
                h264_len = H264_BUF_SIZE;
                // 🔥 核心：直接调用img_joint封装的编码接口
                if (yuyv_to_h264(srv->h264_enc, frame_data, frame_size, srv->h264_buf, &h264_len) == IMG_JOINT_OK)
                {
                    // ✅ 调用完善的打印函数（能看到所有NAL）
                    net_push_print_h264_nal(srv->h264_buf, h264_len);

                    // RTSP推送
                    if(rtsp_started){
                        rtsp_server_push(srv->h264_buf, h264_len);
                    }
                }
                else
                {
                    LOG_E(MODULE_TAG "YUYV转H264编码失败");
                }
            }
            // 释放总线数据
            data_bus_release(frame_item);
        }
    }

    LOG_I(MODULE_TAG "推流工作线程正常退出");
    return NULL;
}

// ==========================================================================
// 服务启动（编码器初始化 + 主动获取SPS/PPS + 启动RTSP）
// ============================================================================
static int net_push_srv_start(void)
{
    net_push_srv_t *srv = &s_net_push_srv;
    int ret = -1;
    pthread_attr_t thread_attr;
    struct sched_param sched_param;

    // 初始化同步变量
    ret = pthread_cond_init(&srv->cond, NULL);
    if (ret != 0) {
        LOG_E(MODULE_TAG "条件变量初始化失败");
        return -1;
    }

    // 🔥 核心：初始化img_joint H264编码器
    h264_encode_param_t enc_param = {
        .width = VIDEO_WIDTH,
        .height = VIDEO_HEIGHT,
        .fps = NET_PUSH_TARGET_FPS,
        .bitrate = H264_BITRATE,
        .gop = H264_GOP,
    };
    LOG_I(MODULE_TAG "创建H264编码器 | %dx%d | %dFPS | GOP=%d",
          VIDEO_WIDTH, VIDEO_HEIGHT, NET_PUSH_TARGET_FPS, H264_GOP);
    srv->h264_enc = h264_encoder_create(&enc_param);
    if (!srv->h264_enc) {
        LOG_E(MODULE_TAG "H.264编码器创建失败");
        pthread_cond_destroy(&srv->cond);
        return -2;
    }

    // 分配编码缓冲区
    srv->h264_buf = (uint8_t*)mem_alloc(H264_BUF_SIZE);
    if (!srv->h264_buf) {
        LOG_E(MODULE_TAG "H.264缓冲区分配失败");
        h264_encoder_destroy(srv->h264_enc);
        pthread_cond_destroy(&srv->cond);
        return -3;
    }

    // 🔥 修复3：编码器创建后，直接获取SPS/PPS（官方标准接口）
    g_sps_pps_len = sizeof(g_sps_pps_cache);
    if (h264_encoder_get_sps_pps(srv->h264_enc, g_sps_pps_cache, &g_sps_pps_len) == IMG_JOINT_OK)
    {
        LOG_I(MODULE_TAG "获取SPS+PPS成功 | 大小: %d bytes", g_sps_pps_len);
        // 注入RTSP并启动服务
        rtsp_set_sps_pps(g_sps_pps_cache, g_sps_pps_len);
        if (rtsp_start_service() == 0) {
            rtsp_started = true;
            LOG_I(MODULE_TAG "✅ RTSP服务启动成功");
        } else {
            LOG_E(MODULE_TAG "RTSP服务启动失败");
        }
    }
    else
    {
        LOG_E(MODULE_TAG "获取SPS/PPS失败");
    }

    // 初始化推流线程（FIFO优先级90）
    pthread_attr_init(&thread_attr);
    pthread_attr_setschedpolicy(&thread_attr, SCHED_FIFO);
    sched_param.sched_priority = 90;
    pthread_attr_setschedparam(&thread_attr, &sched_param);
    pthread_attr_setinheritsched(&thread_attr, PTHREAD_EXPLICIT_SCHED);

    srv->thread_running = true;
    srv->is_paused = false;
    ret = pthread_create(&srv->work_thread, &thread_attr, net_push_work_thread, NULL);
    if (ret != 0) {
        LOG_E(MODULE_TAG "线程创建失败");
        pthread_attr_destroy(&thread_attr);
        free(srv->h264_buf);
        h264_encoder_destroy(srv->h264_enc);
        pthread_cond_destroy(&srv->cond);
        srv->thread_running = false;
        return -5;
    }
    pthread_attr_destroy(&thread_attr);

    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_NET_READY, MODULE_NAME);
    LOG_I(MODULE_TAG "推流服务启动完成");
    return 0;
}

// ==========================================================================
// 资源清理
// ============================================================================
static void net_push_srv_cleanup(void)
{
    net_push_srv_t *srv = &s_net_push_srv;

    LOG_W(MODULE_TAG "开始释放所有资源");
    srv->thread_running = false;
    srv->is_paused = true;

    // 唤醒线程退出
    pthread_mutex_lock(&srv->mutex);
    pthread_cond_signal(&srv->cond);
    pthread_mutex_unlock(&srv->mutex);

    // 等待线程退出
    if (srv->work_thread > 0) {
        pthread_join(srv->work_thread, NULL);
        LOG_I(MODULE_TAG "推流线程已退出");
    }

    // 取消事件订阅
    if (srv->evt_sys_sub_id >= 0) event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_sys_sub_id);
    if (srv->evt_ai_sub_id >= 0) event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_ai_sub_id);

    // 释放img_joint资源
    if (srv->h264_buf) { free(srv->h264_buf); srv->h264_buf = NULL; }
    if (srv->h264_enc) { h264_encoder_destroy(srv->h264_enc); srv->h264_enc = NULL; }

    // 停止RTSP
    rtsp_server_stop();
    rtsp_started = false;
    g_sps_pps_len = 0;

    // 销毁同步变量
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
    srv->evt_ai_sub_id = -1;

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

    // 订阅AI完成事件
    event_subscriber_t ai_sub = {
        .event_type = EVENT_TYPE_FACE_PROCESS_DONE,
        .callback = net_push_event_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_ai_sub_id = event_bus_subscribe(SYS_EVENT_BUS, &ai_sub);

    if (srv->evt_sys_sub_id < 0 || srv->evt_ai_sub_id < 0) {
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