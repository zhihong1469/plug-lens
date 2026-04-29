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
#define V4L2_APP_MAX_POOL_SIZE  32
#define V4L2_APP_DEFAULT_QUEUE_SIZE 4  // 【新增】默认队列大小

// ==========================================================================
// 内部帧池项结构体
// ==========================================================================
typedef struct {
    v4l2_video_frame_t frame;
    bool is_valid;
    bool in_use;
} app_frame_item_t;

// ==========================================================================
// 内部静态变量
// ==========================================================================
static bool g_is_init = false;
static bool g_is_running = false;
static v4l2_app_config_t g_config;

static app_frame_item_t *g_frame_pool = NULL;
static uint32_t g_pool_size = 0;
static pthread_mutex_t g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

static Queue_t g_frame_queue;
static void **g_queue_buffer = NULL;

static pthread_t g_capture_thread;
static volatile bool g_thread_should_exit = false;
static volatile bool g_device_error = false; // 【新增】硬件错误标志位

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

    // 【修复 1】强制设置默认值，防止 queue_size 为 0
    if (g_config.queue_size == 0) {
        g_config.queue_size = V4L2_APP_DEFAULT_QUEUE_SIZE;
        LOG_W("Queue size not set, using default: %u", g_config.queue_size);
    }

    // 1. 初始化底层 V4L2
    v4l2_video_err_t err = v4l2_video_init(&g_config.v4l2_cfg);
    if (err != V4L2_VIDEO_OK) {
        LOG_E("V4L2 init failed: %s", v4l2_video_err_str(err));
        return -1;
    }

    // 2. 确定帧池大小
    g_pool_size = g_config.queue_size;
    if (g_pool_size > g_config.v4l2_cfg.buf_count) {
        g_pool_size = g_config.v4l2_cfg.buf_count;
    }
    if (g_pool_size > V4L2_APP_MAX_POOL_SIZE) {
        g_pool_size = V4L2_APP_MAX_POOL_SIZE;
    }
    
    // 【修复 2】二次校验，防止为 0
    if (g_pool_size == 0) {
        LOG_E("Fatal: Pool size is zero!");
        goto error_v4l2_deinit;
    }

    LOG_I("Frame pool size: %u", g_pool_size);

    // 3. 分配帧池内存
    g_frame_pool = (app_frame_item_t*)calloc(g_pool_size, sizeof(app_frame_item_t));
    if (g_frame_pool == NULL) {
        LOG_E("Failed to alloc frame pool");
        goto error_v4l2_deinit;
    }

    // 4. 分配环形队列缓冲区
    g_queue_buffer = (void**)calloc(g_pool_size + 1, sizeof(void*));
    if (g_queue_buffer == NULL) {
        LOG_E("Failed to alloc queue buffer");
        goto error_free_pool;
    }

    // 5. 初始化环形队列
    Queue_Init(&g_frame_queue, g_queue_buffer, g_pool_size + 1);

    g_device_error = false;
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

    v4l2_video_err_t err = v4l2_video_start();
    if (err != V4L2_VIDEO_OK) {
        LOG_E("V4L2 start failed: %s", v4l2_video_err_str(err));
        return -1;
    }

    g_thread_should_exit = false;
    Queue_Clear(&g_frame_queue);

    pthread_mutex_lock(&g_pool_mutex);
    for (uint32_t i = 0; i < g_pool_size; i++) {
        g_frame_pool[i].in_use = false;
        g_frame_pool[i].is_valid = false;
    }
    pthread_mutex_unlock(&g_pool_mutex);

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
    
    // 【新增】检查硬件错误标志
    if (g_device_error) {
        LOG_E("Device error detected, cannot get frame");
        return -1;
    }

    void *item_ptr = NULL;
    
    if (timeout_ms == 0) {
        if (Queue_Get(&g_frame_queue, &item_ptr) != QUEUE_OK) {
            return -1;
        }
    } else if (timeout_ms < 0) {
        while (Queue_Get(&g_frame_queue, &item_ptr) != QUEUE_OK) {
            usleep(10000);
            if (!g_is_running || g_device_error) return -1;
        }
    } else {
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

    app_frame_item_t *item = (app_frame_item_t*)
        ((uint8_t*)frame - offsetof(app_frame_item_t, frame));

    // 即使硬件出错，也要尝试归还驱动
    v4l2_video_put_frame(&item->frame);
    _pool_return_item(item);
}

int v4l2_app_stop(void)
{
    if (!g_is_init) return 0;
    if (!g_is_running) return 0;

    LOG_I("Stopping V4L2 capture...");

    g_thread_should_exit = true;

    if (pthread_join(g_capture_thread, NULL) != 0) {
        LOG_E("Failed to join thread: %s", strerror(errno));
    }

    // 【修复】即使 stop 失败，也要继续清理，不要返回错误阻塞流程
    v4l2_video_stop(); // 忽略返回值

    g_is_running = false;
    LOG_I("V4L2 capture stopped");
    return 0;
}

void v4l2_app_deinit(void)
{
    if (!g_is_init) return;

    if (g_is_running) {
        v4l2_app_stop();
    }

    LOG_I("Deinitializing V4L2 App...");

    if (g_queue_buffer != NULL) {
        free(g_queue_buffer);
        g_queue_buffer = NULL;
    }

    if (g_frame_pool != NULL) {
        free(g_frame_pool);
        g_frame_pool = NULL;
    }

    v4l2_video_deinit();

    g_is_init = false;
    g_pool_size = 0;
    g_device_error = false;
    LOG_I("V4L2 App deinit success");
}

int v4l2_app_save_yuv(const v4l2_video_frame_t *frame, const char *save_dir)
{
    if (frame == NULL || save_dir == NULL) return -1;
    if (frame->data == NULL || frame->length == 0) return -1;

    char filepath[256];
    static uint32_t file_counter = 0;

    snprintf(filepath, sizeof(filepath), "%s/frame_%06u.yuv", save_dir, file_counter++);
    return v4l2_video_dump_yuv(frame, filepath) == V4L2_VIDEO_OK ? 0 : -1;
}

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

static void* _capture_thread_func(void *arg)
{
    (void)arg;
    LOG_I("Capture thread running");
    int consecutive_errors = 0;

    while (!g_thread_should_exit) {
        // 检查硬件错误标志
        if (g_device_error) {
            LOG_E("Device error flag set, exiting capture thread");
            break;
        }

        app_frame_item_t *item = _pool_get_free_item();
        if (item == NULL) {
            // 帧池耗尽，说明消费者处理太慢
            usleep(20000);
            continue;
        }

        // 从 V4L2 获取帧
        v4l2_video_err_t err = v4l2_video_get_frame(&item->frame);
        if (err != V4L2_VIDEO_OK) {
            if (err != V4L2_VIDEO_ERR_POLL) {
                LOG_E("Get frame failed: %s (consecutive=%d)", 
                      v4l2_video_err_str(err), ++consecutive_errors);
                
                // 【新增】连续错误超过 5 次，触发硬件错误保护
                if (consecutive_errors > 5) {
                    g_device_error = true;
                    LOG_E("Too many errors, marking device as faulty");
                    _pool_return_item(item);
                    break;
                }
            } else {
                // 正常超时，重置计数
                consecutive_errors = 0;
            }
            
            _pool_return_item(item);
            usleep(10000);
            continue;
        }

        // 成功获取帧
        consecutive_errors = 0;
        item->is_valid = true;

        // 【新增】调试日志：打印获取到的帧信息
        LOG_D("Capture thread got frame: idx=%u, len=%u", 
              item->frame.index, item->frame.length);

        // 推入队列
        if (Queue_Put(&g_frame_queue, (void*)item) != QUEUE_OK) {
            LOG_W("Queue full, dropping frame");
            v4l2_video_put_frame(&item->frame);
            _pool_return_item(item);
            continue;
        }
    }

    LOG_I("Capture thread exiting");
    return NULL;
}

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

static void _pool_return_item(app_frame_item_t *item)
{
    if (item == NULL) return;

    pthread_mutex_lock(&g_pool_mutex);
    item->in_use = false;
    item->is_valid = false;
    pthread_mutex_unlock(&g_pool_mutex);
}

static int _wait_queue_not_empty(int timeout_ms)
{
    struct timeval start, now;
    gettimeofday(&start, NULL);

    while (1) {
        if (!Queue_IsEmpty(&g_frame_queue)) {
            return 0;
        }
        if (!g_is_running || g_device_error) {
            return -1;
        }

        gettimeofday(&now, NULL);
        int elapsed = (now.tv_sec - start.tv_sec) * 1000 + 
                      (now.tv_usec - start.tv_usec) / 1000;
        
        if (elapsed >= timeout_ms) {
            return -1;
        }

        usleep(5000);
    }
}