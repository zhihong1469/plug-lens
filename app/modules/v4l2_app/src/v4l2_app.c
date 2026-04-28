#include "v4l2_app.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define V4L2_APP_MAX_POOL_SIZE  32  // 帧池最大容量

// ==========================================================================
// 内部帧池项结构体
// ==========================================================================
typedef struct {
    v4l2_video_frame_t frame;  // V4L2 帧数据
    bool is_valid;              // 该帧是否包含有效数据
    bool in_use;                // 标记：是否正在被消费者（AI）使用
} app_frame_item_t;

// ==========================================================================
// 内部静态变量（模块状态管理）
// ==========================================================================
static bool g_is_init = false;
static bool g_is_running = false;
static v4l2_app_config_t g_config;

// 帧池相关
static app_frame_item_t *g_frame_pool = NULL;
static uint32_t g_pool_size = 0;
static pthread_mutex_t g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

// 环形队列相关
static Queue_t g_frame_queue;
static void **g_queue_buffer = NULL;

// 采集线程相关
static pthread_t g_capture_thread;
static volatile bool g_thread_should_exit = false;

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static void* _capture_thread_func(void *arg);
static app_frame_item_t* _pool_get_free_item(void);
static void _pool_return_item(app_frame_item_t *item);
static int _wait_queue_not_empty(int timeout_ms);

// ==========================================================================
// 对外 API 实现
// ==========================================================================

int v4l2_app_init(const v4l2_app_config_t *config)
{
    if (config == NULL || config->v4l2_cfg.dev_path == NULL) {
        LOG_E("Invalid param");
        return -1;
    }

    if (g_is_init) {
        LOG_E("Already initialized");
        return -1;
    }

    LOG_I("Initializing V4L2 App...");
    memset(&g_config, 0, sizeof(g_config));
    memcpy(&g_config, config, sizeof(v4l2_app_config_t));

    // 1. 初始化底层 V4L2 驱动
    v4l2_video_err_t err = v4l2_video_init(&g_config.v4l2_cfg);
    if (err != V4L2_VIDEO_OK) {
        LOG_E("V4L2 init failed: %s", v4l2_video_err_str(err));
        return -1;
    }

    // 2. 确定帧池大小（队列大小 <= V4L2 缓冲区数量）
    g_pool_size = g_config.queue_size;
    if (g_pool_size > g_config.v4l2_cfg.buf_count) {
        g_pool_size = g_config.v4l2_cfg.buf_count;
    }
    if (g_pool_size > V4L2_APP_MAX_POOL_SIZE) {
        g_pool_size = V4L2_APP_MAX_POOL_SIZE;
    }
    LOG_I("Frame pool size: %u", g_pool_size);

    // 3. 分配帧池内存
    g_frame_pool = (app_frame_item_t*)calloc(g_pool_size, sizeof(app_frame_item_t));
    if (g_frame_pool == NULL) {
        LOG_E("Failed to alloc frame pool");
        goto error_v4l2_deinit;
    }

    // 4. 分配环形队列缓冲区（存指针）
    g_queue_buffer = (void**)calloc(g_pool_size + 1, sizeof(void*)); // +1 是环形队列经典设计
    if (g_queue_buffer == NULL) {
        LOG_E("Failed to alloc queue buffer");
        goto error_free_pool;
    }

    // 5. 初始化环形队列
    Queue_Init(&g_frame_queue, g_queue_buffer, g_pool_size + 1);

    g_is_init = true;
    LOG_I("V4L2 App init success");
    return 0;

error_free_pool:
    free(g_frame_pool);
    g_frame_pool = NULL;
error_v4l2_deinit:
    v4l2_video_deinit();
    return -1;
}

int v4l2_app_start(void)
{
    if (!g_is_init) {
        LOG_E("Not initialized");
        return -1;
    }
    if (g_is_running) {
        LOG_W("Already running");
        return 0;
    }

    LOG_I("Starting V4L2 capture...");

    // 1. 启动底层 V4L2 流
    v4l2_video_err_t err = v4l2_video_start();
    if (err != V4L2_VIDEO_OK) {
        LOG_E("V4L2 start failed: %s", v4l2_video_err_str(err));
        return -1;
    }

    // 2. 重置状态
    g_thread_should_exit = false;
    Queue_Clear(&g_frame_queue);

    // 3. 清空帧池状态
    pthread_mutex_lock(&g_pool_mutex);
    for (uint32_t i = 0; i < g_pool_size; i++) {
        g_frame_pool[i].in_use = false;
        g_frame_pool[i].is_valid = false;
    }
    pthread_mutex_unlock(&g_pool_mutex);

    // 4. 创建采集线程
    if (pthread_create(&g_capture_thread, NULL, _capture_thread_func, NULL) != 0) {
        LOG_E("Failed to create capture thread: %s", strerror(errno));
        v4l2_video_stop();
        return -1;
    }

    g_is_running = true;
    LOG_I("Capture thread started");
    return 0;
}

int v4l2_app_get_frame(v4l2_video_frame_t **frame, int timeout_ms)
{
    if (frame == NULL) return -1;
    if (!g_is_init) return -1;

    void *item_ptr = NULL;
    
    // 根据超时策略等待
    if (timeout_ms == 0) {
        // 非阻塞
        if (Queue_Get(&g_frame_queue, &item_ptr) != QUEUE_OK) {
            return -1;
        }
    } else if (timeout_ms < 0) {
        // 无限阻塞
        while (Queue_Get(&g_frame_queue, &item_ptr) != QUEUE_OK) {
            usleep(10000); // 10ms 轮询
            if (!g_is_running) return -1;
        }
    } else {
        // 限时阻塞
        if (_wait_queue_not_empty(timeout_ms) != 0) {
            return -1;
        }
        if (Queue_Get(&g_frame_queue, &item_ptr) != QUEUE_OK) {
            return -1;
        }
    }

    if (item_ptr == NULL) return -1;

    app_frame_item_t *item = (app_frame_item_t*)item_ptr;
    *frame = &item->frame;
    return 0;
}

void v4l2_app_release_frame(v4l2_video_frame_t *frame)
{
    if (frame == NULL || !g_is_init) return;

    // 通过结构体指针偏移找回父结构体（container_of 思想）
    app_frame_item_t *item = (app_frame_item_t*)
        ((uint8_t*)frame - offsetof(app_frame_item_t, frame));

    // 1. 先把帧归还回 V4L2 驱动（非常重要！）
    v4l2_video_put_frame(&item->frame);

    // 2. 归还回帧池
    _pool_return_item(item);
}

int v4l2_app_stop(void)
{
    if (!g_is_init) return 0;
    if (!g_is_running) return 0;

    LOG_I("Stopping V4L2 capture...");

    // 1. 通知线程退出
    g_thread_should_exit = true;

    // 2. 等待线程结束
    if (pthread_join(g_capture_thread, NULL) != 0) {
        LOG_E("Failed to join thread: %s", strerror(errno));
    }

    // 3. 停止底层 V4L2 流
    v4l2_video_err_t err = v4l2_video_stop();
    if (err != V4L2_VIDEO_OK) {
        LOG_E("V4L2 stop failed: %s", v4l2_video_err_str(err));
    }

    g_is_running = false;
    LOG_I("V4L2 capture stopped");
    return 0;
}

void v4l2_app_deinit(void)
{
    if (!g_is_init) return;

    // 确保先停止
    if (g_is_running) {
        v4l2_app_stop();
    }

    LOG_I("Deinitializing V4L2 App...");

    // 1. 释放队列缓冲区
    if (g_queue_buffer != NULL) {
        free(g_queue_buffer);
        g_queue_buffer = NULL;
    }

    // 2. 释放帧池
    if (g_frame_pool != NULL) {
        free(g_frame_pool);
        g_frame_pool = NULL;
    }

    // 3. 反初始化 V4L2
    v4l2_video_deinit();

    g_is_init = false;
    g_pool_size = 0;
    LOG_I("V4L2 App deinit success");
}

int v4l2_app_save_yuv(const v4l2_video_frame_t *frame, const char *save_dir)
{
    if (frame == NULL || save_dir == NULL) return -1;
    if (frame->data == NULL || frame->length == 0) return -1;

    char filepath[256];
    static uint32_t file_counter = 0;

    // 生成文件名：save_dir/frame_000001.yuv
    snprintf(filepath, sizeof(filepath), "%s/frame_%06u.yuv", save_dir, file_counter++);

    FILE *fp = fopen(filepath, "wb");
    if (fp == NULL) {
        LOG_E("Failed to open file: %s", filepath);
        return -1;
    }

    size_t written = fwrite(frame->data, 1, frame->length, fp);
    if (written != frame->length) {
        LOG_E("Failed to write full file");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    LOG_I("Saved YUV frame: %s (%u bytes)", filepath, frame->length);
    return 0;
}

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

/**
 * @brief 采集线程主函数
 */
static void* _capture_thread_func(void *arg)
{
    (void)arg;
    LOG_I("Capture thread running");

    while (!g_thread_should_exit) {
        // 1. 从帧池获取一个空闲项
        app_frame_item_t *item = _pool_get_free_item();
        if (item == NULL) {
            // 帧池耗尽（消费者处理太慢），稍等重试
            usleep(20000);
            continue;
        }

        // 2. 从 V4L2 驱动获取一帧数据（阻塞）
        v4l2_video_err_t err = v4l2_video_get_frame(&item->frame);
        if (err != V4L2_VIDEO_OK) {
            if (err != V4L2_VIDEO_ERR_POLL) { // 忽略正常的超时/信号打断
                LOG_E("Get frame failed: %s", v4l2_video_err_str(err));
            }
            _pool_return_item(item);
            usleep(10000);
            continue;
        }

        item->is_valid = true;

        // 3. 推入环形队列（给消费者）
        if (Queue_Put(&g_frame_queue, (void*)item) != QUEUE_OK) {
            // 队列满了（消费者处理太慢），丢弃这一帧
            LOG_W("Queue full, dropping frame");
            v4l2_video_put_frame(&item->frame); // 必须归还驱动
            _pool_return_item(item);
            continue;
        }

        // 成功，继续下一帧
    }

    LOG_I("Capture thread exiting");
    return NULL;
}

/**
 * @brief 从帧池获取一个空闲项
 */
static app_frame_item_t* _pool_get_free_item(void)
{
    app_frame_item_t *ret = NULL;

    pthread_mutex_lock(&g_pool_mutex);
    for (uint32_t i = 0; i < g_pool_size; i++) {
        if (!g_frame_pool[i].in_use) {
            g_frame_pool[i].in_use = true;
            g_frame_pool[i].is_valid = false;
            ret = &g_frame_pool[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_pool_mutex);

    return ret;
}

/**
 * @brief 归还项到帧池
 */
static void _pool_return_item(app_frame_item_t *item)
{
    if (item == NULL) return;

    pthread_mutex_lock(&g_pool_mutex);
    item->in_use = false;
    item->is_valid = false;
    pthread_mutex_unlock(&g_pool_mutex);
}

/**
 * @brief 限时等待队列非空
 */
static int _wait_queue_not_empty(int timeout_ms)
{
    struct timeval start, now;
    gettimeofday(&start, NULL);

    while (1) {
        if (!Queue_IsEmpty(&g_frame_queue)) {
            return 0;
        }
        if (!g_is_running) {
            return -1;
        }

        gettimeofday(&now, NULL);
        int elapsed = (now.tv_sec - start.tv_sec) * 1000 + 
                      (now.tv_usec - start.tv_usec) / 1000;
        
        if (elapsed >= timeout_ms) {
            return -1; // 超时
        }

        usleep(5000); // 5ms 轮询间隔
    }
}
