#include "camera_base.h"
#include "camera_usb.h"
#include "frame_link.h"
#include "log.h"
// 1. 引入AI模块头文件
#include "ai_model_mnn.hpp"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

// ==========================================================================
// 全局配置（和你的 vision_ai_config.h 对齐）
// ==========================================================================
#define TEST_DEV_PATH       "/dev/video1"
// 摄像头最低分辨率
#define TEST_WIDTH          640
#define TEST_HEIGHT         360
#define TEST_FPS            30
#define TEST_FORMAT         V4L2_PIX_FMT_YUYV

#define TEST_POOL_CAPACITY  4
#define TEST_QUEUE_CAPACITY 2
#define TEST_RUN_TIME       30      // 延长测试时间，方便看AI效果

// AI 模型配置（你提供的参数）
#define CONFIG_AI_MODEL_PATH "./RFB-320-quant-KL-5792.mnn"
#define CONFIG_AI_INPUT_W    320
#define CONFIG_AI_INPUT_H    240
#define CONFIG_AI_SCORE_THRESH 0.65f
#define CONFIG_AI_IOU_THRESH   0.3f
// 最大检测人脸数
#define MAX_FACE_NUM 5

// ==========================================================================
// 全局运行控制
// ==========================================================================
static volatile bool g_running = true;
static camera_base_t *g_cam = NULL;
static frame_link_handle_t g_fl = NULL;
static ai_model_handle_t *g_ai_model = NULL;  // AI模型句柄

// 统计信息
static uint32_t g_total_capture = 0;
static uint32_t g_total_consume = 0;
static uint32_t g_total_drop = 0;
static uint32_t g_face_detected_count = 0;    // 检测到人脸总次数

// ==========================================================================
// 信号处理：Ctrl+C 安全退出
// ==========================================================================
static void sigint_handler(int sig)
{
    (void)sig;
    printf("\n[Test] 收到退出信号，准备安全退出...\n");
    g_running = false;
}

// ==========================================================================
// 采集线程（生产者）【无修改，完全复用】
// ==========================================================================
static void* capture_thread(void *arg)
{
    (void)arg;
    printf("[Test] 采集线程启动\n");

    while (g_running) {
        frame_t *frame = NULL;

        // 1. 从 frame_link 获取空闲帧
        int ret = frame_link_get_free_frame(g_fl, &frame);
        if (ret != 0) {
            usleep(1000);
            continue;
        }

        // 2. 从摄像头采集数据
        void *cam_buf = NULL;
        size_t cam_len = 0;
        ret = camera_get_frame(g_cam, &cam_buf, &cam_len);
        if (ret != 0) {
            frame_link_return_free_frame(g_fl, frame);
            usleep(10000);
            continue;
        }

        // 3. 填充帧信息
        memcpy(frame->data, cam_buf, cam_len);
        frame->width = TEST_WIDTH;
        frame->height = TEST_HEIGHT;
        frame->format = FRAME_FMT_YUYV;
        frame->index = g_total_capture++;

        // 4. 入队
        uint32_t queue_len_before = frame_link_get_queue_count(g_fl);
        ret = frame_link_enqueue_frame(g_fl, frame);
        uint32_t queue_len_after = frame_link_get_queue_count(g_fl);

        if (queue_len_before == TEST_QUEUE_CAPACITY && queue_len_after == TEST_QUEUE_CAPACITY) {
            g_total_drop++;
        }

        if (g_total_capture % 10 == 0) {
            printf("[Capture] 采集第%u帧 | 队列:%u | 空闲帧:%u | 丢帧:%u\n",
                   frame->index, queue_len_after,
                   frame_link_get_free_count(g_fl), g_total_drop);
        }

        usleep(1000000 / TEST_FPS);
    }

    printf("[Test] 采集线程退出\n");
    return NULL;
}

// ==========================================================================
// 消费线程（AI推理核心）【重写为真实人脸检测】
// ==========================================================================
static void* consume_thread(void *arg)
{
    (void)arg;
    printf("[Test] AI消费线程启动 → 实时人脸检测\n");

    while (g_running) {
        frame_t *frame = NULL;
        int ret = frame_link_dequeue_frame(g_fl, &frame);
        if (ret != 0) {
            usleep(1000);
            continue;
        }

        g_total_consume++;

        // ======================
        // 【核心】AI 人脸检测（直接调用你现成的YUYV推理接口）
        // ======================
        FaceInfo_C faces[MAX_FACE_NUM] = {0};
        int face_num = 0;

        int ai_ret = ai_model_mnn_infer_yuyv(
            frame->data,
            frame->width,
            frame->height,
            faces,
            MAX_FACE_NUM,
            &face_num
        );

        // 检测成功
        if (ai_ret == MNN_FACE_OK && face_num > 0) {
            g_face_detected_count++;
            printf("\n[AI DETECT] 帧:%u → 检测到 %d 张人脸!\n",
                   frame->index, face_num);

            // 遍历所有人脸 + 坐标映射(640x360)
            for (int i = 0; i < face_num; i++) {
                // 自动映射AI输出坐标 → 摄像头原始分辨率
                ai_model_mnn_map_face(&faces[i], frame->width, frame->height);
                printf("  人脸%d → (%.1f,%.1f) → (%.1f,%.1f) 置信度:%.2f\n",
                       i+1,
                       faces[i].x1, faces[i].y1,
                       faces[i].x2, faces[i].y2,
                       faces[i].score);
            }
        }

        // 每5帧打印一次状态
        if (g_total_consume % 5 == 0 && face_num == 0) {
            printf("[Consume] 处理第%u帧 | 无人脸\n", frame->index);
        }

        // 归还帧
        frame_link_release_frame(g_fl, frame);
    }

    printf("[Test] AI消费线程退出\n");
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
    printf("  摄像头 + FrameLink + AI人脸检测 测试\n");
    printf("  摄像头: %dx%d YUYV\n", TEST_WIDTH, TEST_HEIGHT);
    printf("  AI输入: %dx%d\n", CONFIG_AI_INPUT_W, CONFIG_AI_INPUT_H);
    printf("  模型: %s\n", CONFIG_AI_MODEL_PATH);
    printf("========================================\n\n");

    log_init(LOG_LEVEL_INFO);

    // ====================== 1. 初始化摄像头 ======================
    printf("[Step 1] 初始化摄像头...\n");
    g_cam = camera_usb_create(TEST_DEV_PATH, TEST_WIDTH, TEST_HEIGHT, TEST_FORMAT, TEST_FPS);
    if (!g_cam) {
        printf("[Error] 创建摄像头失败\n");
        return -1;
    }

    ret = camera_init(g_cam);
    if (ret != 0) {
        camera_usb_destroy(g_cam);
        return -1;
    }
    printf("[OK] 摄像头初始化成功\n");

    // ====================== 2. 初始化 FrameLink ======================
    printf("[Step 2] 初始化 FrameLink...\n");
    frame_link_config_t fl_cfg = {
        .max_frame_size = TEST_WIDTH * TEST_HEIGHT * 2,
        .pool_capacity = TEST_POOL_CAPACITY,
        .queue_capacity = TEST_QUEUE_CAPACITY
    };
    ret = frame_link_init(&fl_cfg, &g_fl);
    if (ret != 0) {
        camera_usb_destroy(g_cam);
        return -1;
    }
    printf("[OK] FrameLink初始化成功\n");

    // ====================== 3. 初始化 AI 模型 ======================
    printf("[Step 3] 初始化 MNN 人脸检测模型...\n");
    ai_model_config_t ai_cfg = {
        .model_path = CONFIG_AI_MODEL_PATH,
        .input_width = CONFIG_AI_INPUT_W,
        .input_height = CONFIG_AI_INPUT_H,
        .score_thresh = CONFIG_AI_SCORE_THRESH,
        .iou_thresh = CONFIG_AI_IOU_THRESH
    };
    g_ai_model = ai_model_mnn_create(&ai_cfg);
    if (!g_ai_model) {
        printf("[Error] AI模型创建失败\n");
        frame_link_deinit(g_fl);
        camera_usb_destroy(g_cam);
        return -1;
    }
    // 初始化模型
    ret = ai_model_init(g_ai_model);
    if (ret != AI_MODEL_OK) {
        printf("[Error] AI模型初始化失败 错误码:%d\n", ret);
        ai_model_destroy(g_ai_model);
        frame_link_deinit(g_fl);
        camera_usb_destroy(g_cam);
        return -1;
    }
    printf("[OK] AI模型初始化成功 ✅\n");

    // ====================== 4. 启动采集 ======================
    printf("[Step 4] 启动摄像头采集...\n");
    ret = camera_start_capture(g_cam);
    if (ret != 0) {
        printf("[Error] 启动采集失败\n");
        goto err_release;
    }
    printf("[OK] 摄像头已启动\n");

    // ====================== 5. 启动双线程 ======================
    printf("[Step 5] 启动采集 + AI线程...\n");
    pthread_create(&cap_tid, NULL, capture_thread, NULL);
    pthread_create(&con_tid, NULL, consume_thread, NULL);
    printf("[OK] 系统运行中...\n\n");

    // ====================== 运行测试 ======================
    for (int i = 0; i < TEST_RUN_TIME && g_running; i++) {
        sleep(1);
    }

    // ====================== 停止 ======================
    printf("\n[Test] 停止系统...\n");
    g_running = false;
    pthread_join(cap_tid, NULL);
    pthread_join(con_tid, NULL);

    // ====================== 最终统计 ======================
    printf("\n========================================\n");
    printf("            测试完成统计\n");
    printf("========================================\n");
    printf("总采集帧数: %u\n", g_total_capture);
    printf("总处理帧数: %u\n", g_total_consume);
    printf("总丢帧数: %u\n", g_total_drop);
    printf("检测到人脸次数: %u\n", g_face_detected_count);
    printf("========================================\n\n");

    // ====================== 释放资源 ======================
err_release:
    printf("[Step 6] 释放所有资源...\n");
    camera_stop_capture(g_cam);
    if (g_ai_model) ai_model_deinit(g_ai_model);
    frame_link_deinit(g_fl);
    camera_usb_destroy(g_cam);
    log_deinit();

    printf("[OK] 程序正常退出\n");
    return 0;
}