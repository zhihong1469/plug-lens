#include "camera_base.h"
#include "camera_usb.h"
#include "frame_link.h"
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "net_push_srv.h"
// AI模块头文件
#include "ai_model_mnn.hpp"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

// ==========================================================================
// 全局配置
// ==========================================================================
#define TEST_DEV_PATH       "/dev/video1"
#define TEST_WIDTH          640
#define TEST_HEIGHT         360
#define TEST_FPS            30
#define TEST_FORMAT         V4L2_PIX_FMT_YUYV

#define TEST_POOL_CAPACITY  4
#define TEST_QUEUE_CAPACITY 2
#define TEST_RUN_TIME       60

// AI配置
#define CONFIG_AI_MODEL_PATH "./RFB-320-quant-KL-5792.mnn"
#define CONFIG_AI_INPUT_W    320
#define CONFIG_AI_INPUT_H    240
#define CONFIG_AI_SCORE_THRESH 0.65f
#define CONFIG_AI_IOU_THRESH   0.3f
#define MAX_FACE_NUM 5

// 网络推流配置
#define NET_PUSH_PORT        8888
#define NET_PUSH_QUEUE_SIZE  2

// DataBus配置
#define DATA_BUS_MAX_ITEMS       4
#define DATA_BUS_MAX_ITEM_SIZE  (TEST_WIDTH * TEST_HEIGHT * 2)
#define DATA_BUS_MAX_SUBSCRIBERS 3

// EventBus配置
#define EVENT_BUS_MAX_SUBSCRIBERS 8

// ==========================================================================
// 全局变量
// ==========================================================================
static volatile bool g_running = true;

// 硬件核心
static camera_base_t        *g_cam = NULL;
static frame_link_handle_t  g_fl = NULL;
static ai_model_handle_t    *g_ai_model = NULL;

// 总线核心
static data_bus_handle_t     g_data_bus = NULL;
static event_bus_handle_t    g_event_bus = NULL;

// 网络推流服务
static net_push_srv_handle_t g_net_push = NULL;

// 统计
static uint32_t g_total_capture = 0;
static uint32_t g_total_consume = 0;
static uint32_t g_total_drop = 0;
static uint32_t g_face_detected_count = 0;

// ==========================================================================
// 信号处理
// ==========================================================================
static void sigint_handler(int sig)
{
    (void)sig;
    printf("\n[Test] 退出信号，安全停止...\n");
    g_running = false;
    // 发布系统停止事件
    event_bus_publish_simple(g_event_bus, EVENT_TYPE_SYS_STOP, "MAIN");
}

// ==========================================================================
// 采集线程：帧 -> FrameLink + DataBus
// ==========================================================================
static void* capture_thread(void *arg)
{
    (void)arg;
    printf("[Test] 采集线程启动\n");

    while (g_running) {
        frame_t *frame = NULL;
        int ret = frame_link_get_free_frame(g_fl, &frame);
        if (ret != 0) {
            usleep(1000);
            continue;
        }

        // 采集摄像头数据
        void *cam_buf = NULL;
        size_t cam_len = 0;
        ret = camera_get_frame(g_cam, &cam_buf, &cam_len);
        if (ret != 0) {
            frame_link_return_free_frame(g_fl, frame);
            usleep(10000);
            continue;
        }

        // 填充帧信息
        memcpy(frame->data, cam_buf, cam_len);
        frame->width = TEST_WIDTH;
        frame->height = TEST_HEIGHT;
        frame->format = FRAME_FMT_YUYV;
        frame->index = g_total_capture++;

        // --------------------------
        // 1. 入队FrameLink（给AI用）
        // --------------------------
        uint32_t q_before = frame_link_get_queue_count(g_fl);
        frame_link_enqueue_frame(g_fl, frame);
        uint32_t q_after = frame_link_get_queue_count(g_fl);
        if (q_before == TEST_QUEUE_CAPACITY && q_after == TEST_QUEUE_CAPACITY)
            g_total_drop++;

        // --------------------------
        // 2. 发布到DataBus（给推流用）
        // --------------------------
        data_bus_item_handle_t bus_item = NULL;
        ret = data_bus_alloc(g_data_bus,
                            DATA_TYPE_VIDEO_FRAME,
                            cam_len,
                            "CAPTURE",
                            &bus_item);
        if (ret == 0) {
            void *w_ptr = data_bus_get_writable_ptr(bus_item);
            memcpy(w_ptr, frame->data, cam_len);
            data_bus_publish(g_data_bus, bus_item);
        }

        // 日志
        if (g_total_capture % 10 == 0) {
            printf("[Capture] 帧%u | 队列:%u | 丢帧:%u\n",
                   frame->index, q_after, g_total_drop);
        }

        usleep(1000000 / TEST_FPS);
    }

    printf("[Test] 采集线程退出\n");
    return NULL;
}

// ==========================================================================
// AI消费线程
// ==========================================================================
static void* consume_thread(void *arg)
{
    (void)arg;
    printf("[Test] AI人脸检测线程启动\n");

    while (g_running) {
        frame_t *frame = NULL;
        int ret = frame_link_dequeue_frame(g_fl, &frame);
        if (ret != 0) {
            usleep(1000);
            continue;
        }

        g_total_consume++;
        FaceInfo_C faces[MAX_FACE_NUM] = {0};
        int face_num = 0;

        // AI推理
        ai_model_mnn_infer_yuyv(frame->data,
                               frame->width, frame->height,
                               faces, MAX_FACE_NUM, &face_num);

        if (face_num > 0) {
            g_face_detected_count++;
            printf("\n[AI DETECT] 帧%u → 检测到%d人脸\n", frame->index, face_num);
            for (int i = 0; i < face_num; i++) {
                ai_model_mnn_map_face(&faces[i], frame->width, frame->height);
                printf("  %d: (%.1f,%.1f)-(%.1f,%.1f) 置信:%.2f\n",
                       i+1, faces[i].x1, faces[i].y1,
                       faces[i].x2, faces[i].y2, faces[i].score);
            }
        }

        if (g_total_consume % 5 == 0 && face_num == 0) {
            printf("[Consume] 帧%u | 无人脸\n", frame->index);
        }

        frame_link_release_frame(g_fl, frame);
    }

    printf("[Test] AI线程退出\n");
    return NULL;
}

// ==========================================================================
// 主函数
// ==========================================================================
int main(void)
{
    int ret;
    pthread_t cap_tid, con_tid;

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    printf("========================================\n");
    printf("  摄像头 + AI检测 + UDP视频推流 三合一\n");
    printf("  推流地址: UDP 8888\n");
    printf("========================================\n");

    log_init(LOG_LEVEL_INFO);

    // ====================== 1. 初始化EventBus ======================
    event_bus_config_t event_cfg = {
        .max_subscribers = EVENT_BUS_MAX_SUBSCRIBERS
    };
    ret = event_bus_init(&event_cfg);
    if (ret != 0) {
        printf("[Error] EventBus初始化失败\n");
        return -1;
    }

    // ====================== 2. 初始化DataBus ======================
    data_bus_config_t data_cfg = {
        .max_items = DATA_BUS_MAX_ITEMS,
        .max_item_size = DATA_BUS_MAX_ITEM_SIZE,
        .max_subscribers = DATA_BUS_MAX_SUBSCRIBERS
    };
    ret = data_bus_init(&data_cfg, &g_data_bus);
    if (ret != 0) {
        printf("[Error] DataBus初始化失败\n");
        event_bus_deinit(g_event_bus);
        return -1;
    }

    // ====================== 3. 初始化摄像头 ======================
    g_cam = camera_usb_create(TEST_DEV_PATH, TEST_WIDTH, TEST_HEIGHT, TEST_FORMAT, TEST_FPS);
    if (!g_cam || camera_init(g_cam) != 0) {
        printf("[Error] 摄像头失败\n");
        return -1;
    }
    printf("[OK] 摄像头初始化完成\n");

    // ====================== 4. 初始化FrameLink ======================
    frame_link_config_t fl_cfg = {
        .max_frame_size = TEST_WIDTH * TEST_HEIGHT * 2,
        .pool_capacity = TEST_POOL_CAPACITY,
        .queue_capacity = TEST_QUEUE_CAPACITY
    };
    frame_link_init(&fl_cfg, &g_fl);
    printf("[OK] FrameLink初始化完成\n");

    // ====================== 5. 初始化AI模型 ======================
    ai_model_config_t ai_cfg = {
        .model_path = CONFIG_AI_MODEL_PATH,
        .input_width = CONFIG_AI_INPUT_W,
        .input_height = CONFIG_AI_INPUT_H,
        .score_thresh = CONFIG_AI_SCORE_THRESH,
        .iou_thresh = CONFIG_AI_IOU_THRESH
    };
    g_ai_model = ai_model_mnn_create(&ai_cfg);
    if (ai_model_init(g_ai_model) != AI_MODEL_OK) {
        printf("[Error] AI模型失败\n");
        return -1;
    }
    printf("[OK] AI模型初始化完成 ✅\n");

    // ====================== 6. 初始化UDP推流服务 ======================
    net_push_srv_config_t net_cfg = {
        .bind_ip = "0.0.0.0",
        .bind_port = NET_PUSH_PORT,
        .queue_capacity = NET_PUSH_QUEUE_SIZE,
        .enable_log = false,
        .data_bus = g_data_bus,
        .event_bus = g_event_bus
    };
    net_push_srv_create(&net_cfg, &g_net_push);
    net_push_srv_start(g_net_push);
    printf("[OK] UDP推流服务启动 (端口:8888)\n");

    // ====================== 7. 启动采集 ======================
    camera_start_capture(g_cam);
    printf("[OK] 摄像头采集启动\n");

    // ====================== 8. 启动线程 ======================
    pthread_create(&cap_tid, NULL, capture_thread, NULL);
    pthread_create(&con_tid, NULL, consume_thread, NULL);
    printf("[OK] 系统运行中，等待连接...\n\n");

    // ====================== 主线程循环 ======================
    for (int i = 0; i < TEST_RUN_TIME && g_running; i++) {
        event_bus_dispatch(g_event_bus);
        sleep(1);
    }

    // ====================== 停止流程 ======================
    printf("\n[Test] 开始停止系统...\n");
    g_running = false;

    pthread_join(cap_tid, NULL);
    pthread_join(con_tid, NULL);

    // ====================== 资源释放 ======================
    net_push_srv_destroy(g_net_push);
    camera_stop_capture(g_cam);
    ai_model_deinit(g_ai_model);
    frame_link_deinit(g_fl);
    camera_usb_destroy(g_cam);
    data_bus_deinit(g_data_bus);
    event_bus_deinit(g_event_bus);
    log_deinit();

    // ====================== 统计 ======================
    printf("\n========================================\n");
    printf("              运行统计\n");
    printf("========================================\n");
    printf("总采集帧数: %u\n", g_total_capture);
    printf("AI处理帧数: %u\n", g_total_consume);
    printf("总丢帧数: %u\n", g_total_drop);
    printf("人脸检测次数: %u\n", g_face_detected_count);
    printf("推流端口: UDP 8888\n");
    printf("========================================\n");

    printf("[OK] 程序安全退出\n");
    return 0;
}