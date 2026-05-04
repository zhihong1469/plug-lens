#include "frame_link.h"
#include "video_hal.h"
#include "pool.h"       // 【新增】引入通用对象池
#include "queue.h"      // 【新增】引入通用队列
#include "thread.h"     // 【新增】引入通用线程封装
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <sys/poll.h>
#include "log.h"
#include "main.h"

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define FRAME_LINK_MAX_POOL_SIZE 32
#define FRAME_LINK_MAX_QUEUE_SIZE 16

// ==========================================================================
// 内部帧节点结构体（简化，不再需要 in_use/ref_count，由 Pool 管理）
// ==========================================================================
typedef struct {
    video_frame_t frame;       // 帧数据
} frame_node_t;

// ==========================================================================
// 【核心】内部状态结构体（重构版）
// ==========================================================================
typedef struct {
    // HAL层相关
    video_handle_t hal_handle;
    video_capability_t hal_cap;
    frame_link_config_t config;
    int cam_fd;                // 摄像头设备fd（用于poll）

    // 【重构】通用对象池（替代手写帧池）
    Pool_t frame_pool;
    void *pool_memory_buffer;       // 池内存缓冲区
    void **pool_free_list_buffer;   // 池空闲列表缓冲区

    // 【重构】通用队列（替代手写队列）
    Queue_t frame_queue;
    void **queue_buffer;            // 队列缓冲区

    // 队列同步（保留，因为 Queue 组件不做等待）
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_not_empty;
    pthread_cond_t queue_not_full;

    // 【重构】采集线程（使用通用线程封装）
    thread_t capture_thread;
    bool running;
    bool streaming;

    // 全局优雅退出管道读端
    int exit_pipe_read_fd;

    // Service层回调
    frame_link_frame_ready_cb frame_ready_cb;
    void *cb_user_data;
} frame_link_context_t;

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static frame_node_t* _frame_link_alloc_frame(frame_link_context_t *ctx);
static void _frame_link_free_frame(frame_link_context_t *ctx, frame_node_t *node);
static int _frame_link_enqueue(frame_link_context_t *ctx, frame_node_t *node);
static frame_node_t* _frame_link_dequeue(frame_link_context_t *ctx, uint32_t timeout_ms);
static void* _frame_link_capture_thread(void *arg);

// ==========================================================================
// 对外API实现
// ==========================================================================

video_err_t frame_link_init(const frame_link_config_t *config,
                            int exit_pipe_read_fd,
                            frame_link_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL || exit_pipe_read_fd < 0) {
        return VIDEO_ERR_INVALID_PARAM;
    }

    // 分配上下文
    frame_link_context_t *ctx = (frame_link_context_t*)malloc(sizeof(frame_link_context_t));
    if (ctx == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    memset(ctx, 0, sizeof(frame_link_context_t));

    // 拷贝配置 + 初始化退出管道
    memcpy(&ctx->config, config, sizeof(frame_link_config_t));
    ctx->exit_pipe_read_fd = exit_pipe_read_fd;

    // 配置校验
    if (ctx->config.frame_pool_size == 0) ctx->config.frame_pool_size = 8;
    if (ctx->config.frame_pool_size > FRAME_LINK_MAX_POOL_SIZE) ctx->config.frame_pool_size = FRAME_LINK_MAX_POOL_SIZE;
    if (ctx->config.queue_size == 0) ctx->config.queue_size = 4;
    if (ctx->config.queue_size > FRAME_LINK_MAX_QUEUE_SIZE) ctx->config.queue_size = FRAME_LINK_MAX_QUEUE_SIZE;

    // -------------------------------------------------------------------------
    // 【重构】1. 分配并初始化通用对象池
    // -------------------------------------------------------------------------
    size_t pool_mem_size = sizeof(frame_node_t) * ctx->config.frame_pool_size;
    size_t pool_flist_size = sizeof(void*) * ctx->config.frame_pool_size;
    
    ctx->pool_memory_buffer = malloc(pool_mem_size);
    ctx->pool_free_list_buffer = malloc(pool_flist_size);
    
    if (ctx->pool_memory_buffer == NULL || ctx->pool_free_list_buffer == NULL) {
        if (ctx->pool_memory_buffer) free(ctx->pool_memory_buffer);
        if (ctx->pool_free_list_buffer) free(ctx->pool_free_list_buffer);
        free(ctx);
        return VIDEO_ERR_INVALID_PARAM;
    }
    
    Pool_Init(&ctx->frame_pool, 
              sizeof(frame_node_t), 
              ctx->config.frame_pool_size,
              ctx->pool_memory_buffer,
              ctx->pool_free_list_buffer);

    // -------------------------------------------------------------------------
    // 【重构】2. 分配并初始化通用队列
    // -------------------------------------------------------------------------
    size_t queue_buf_size = sizeof(void*) * (ctx->config.queue_size + 1); // +1 是环形队列经典设计
    ctx->queue_buffer = malloc(queue_buf_size);
    
    if (ctx->queue_buffer == NULL) {
        free(ctx->pool_memory_buffer);
        free(ctx->pool_free_list_buffer);
        free(ctx);
        return VIDEO_ERR_INVALID_PARAM;
    }
    
    Queue_Init(&ctx->frame_queue, ctx->queue_buffer, ctx->config.queue_size + 1);

    // -------------------------------------------------------------------------
    // 3. 初始化队列同步锁和条件变量
    // -------------------------------------------------------------------------
    pthread_mutex_init(&ctx->queue_lock, NULL);
    pthread_cond_init(&ctx->queue_not_empty, NULL);
    pthread_cond_init(&ctx->queue_not_full, NULL);

    // -------------------------------------------------------------------------
    // 4. 初始化HAL层
    // -------------------------------------------------------------------------
    video_err_t err = video_open(&ctx->config.hal_config, &ctx->hal_cap, &ctx->hal_handle);
    if (err != VIDEO_OK) {
        free(ctx->queue_buffer);
        free(ctx->pool_memory_buffer);
        free(ctx->pool_free_list_buffer);
        pthread_mutex_destroy(&ctx->queue_lock);
        pthread_cond_destroy(&ctx->queue_not_empty);
        pthread_cond_destroy(&ctx->queue_not_full);
        free(ctx);
        return err;
    }

    // 获取摄像头fd（用于Link层poll）
    ctx->cam_fd = video_get_wait_fd(ctx->hal_handle);
    if (ctx->cam_fd < 0) {
        video_close(ctx->hal_handle);
        free(ctx->queue_buffer);
        free(ctx->pool_memory_buffer);
        free(ctx->pool_free_list_buffer);
        pthread_mutex_destroy(&ctx->queue_lock);
        pthread_cond_destroy(&ctx->queue_not_empty);
        pthread_cond_destroy(&ctx->queue_not_full);
        free(ctx);
        return VIDEO_ERR_INVALID_PARAM;
    }

    ctx->running = false;
    ctx->streaming = false;
    ctx->frame_ready_cb = NULL;
    ctx->cb_user_data = NULL;

    *out_handle = (frame_link_handle_t)ctx;
    return VIDEO_OK;
}

video_err_t frame_link_start(frame_link_handle_t handle)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    if (ctx->running) {
        LOG_I("Frame Link: Already running");
        return VIDEO_OK;
    }

    LOG_I("Frame Link: Starting HAL layer stream...");
    // 启动HAL层流
    video_err_t err = video_start_stream(ctx->hal_handle);
    if (err != VIDEO_OK) {
        LOG_E("Frame Link: Failed to start HAL stream (err=%d)", err);
        return err;
    }
    LOG_I("Frame Link: HAL stream started");
    
    ctx->streaming = true;

    // 【重构】启动采集线程（使用通用线程封装）
    LOG_I("Frame Link: Creating capture thread...");
    ctx->running = true;
    
    thread_attr_t attr;
    thread_attr_init(&attr);
    attr.name = "capture_thread";
    attr.priority = THREAD_PRIORITY_HIGH;
    attr.stack_size = 128 * 1024; // 128KB 栈
    
    thread_err_t terr = thread_create(&ctx->capture_thread,
                                       &attr,
                                       _frame_link_capture_thread,
                                       ctx);
    if (terr != THREAD_OK) {
        LOG_E("Frame Link: Failed to create thread");
        video_stop_stream(ctx->hal_handle);
        ctx->streaming = false;
        ctx->running = false;
        return VIDEO_ERR_INVALID_PARAM;
    }
    
    LOG_I("Frame Link: Capture thread created");
    return VIDEO_OK;
}

video_err_t frame_link_stop(frame_link_handle_t handle)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    if (!ctx->running) {
        return VIDEO_OK;
    }

    // 停止采集线程
    ctx->running = false;
    // 唤醒所有条件变量
    pthread_mutex_lock(&ctx->queue_lock);
    pthread_cond_broadcast(&ctx->queue_not_empty);
    pthread_cond_broadcast(&ctx->queue_not_full);
    pthread_mutex_unlock(&ctx->queue_lock);
    
    // 【重构】等待线程安全退出（使用通用线程封装）
    thread_join(&ctx->capture_thread, NULL);

    // 停止HAL层流
    if (ctx->streaming) {
        video_stop_stream(ctx->hal_handle);
        ctx->streaming = false;
    }

    // 清空队列
    pthread_mutex_lock(&ctx->queue_lock);
    void *item = NULL;
    while (Queue_Get(&ctx->frame_queue, &item) == QUEUE_OK) {
        if (item != NULL) {
            _frame_link_free_frame(ctx, (frame_node_t*)item);
        }
    }
    pthread_mutex_unlock(&ctx->queue_lock);

    return VIDEO_OK;
}

video_err_t frame_link_get_frame(frame_link_handle_t handle,
                                  video_frame_t *frame,
                                  uint32_t timeout_ms)
{
    if (handle == NULL || frame == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    // 从队列取帧
    frame_node_t *node = _frame_link_dequeue(ctx, timeout_ms);
    if (node == NULL) {
        return VIDEO_ERR_POLL;
    }

    // 拷贝帧数据
    memcpy(frame, &node->frame, sizeof(video_frame_t));
    
    // 【重要】释放节点（因为我们已经拷贝了数据）
    _frame_link_free_frame(ctx, node);
    
    return VIDEO_OK;
}

video_err_t frame_link_put_frame(frame_link_handle_t handle,
                                  const video_frame_t *frame)
{
    if (handle == NULL || frame == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    // 归还HAL层缓冲区
    video_put_frame(ctx->hal_handle, frame);
    
    return VIDEO_OK;
}

video_err_t frame_link_register_frame_ready_cb(frame_link_handle_t handle,
                                                 frame_link_frame_ready_cb cb,
                                                 void *user_data)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    pthread_mutex_lock(&ctx->queue_lock);
    ctx->frame_ready_cb = cb;
    ctx->cb_user_data = user_data;
    pthread_mutex_unlock(&ctx->queue_lock);

    return VIDEO_OK;
}

video_err_t frame_link_deinit(frame_link_handle_t handle)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    // 先停止
    frame_link_stop(handle);

    // 关闭HAL层
    video_close(ctx->hal_handle);

    // 释放资源
    pthread_mutex_destroy(&ctx->queue_lock);
    pthread_cond_destroy(&ctx->queue_not_empty);
    pthread_cond_destroy(&ctx->queue_not_full);

    // 【重构】释放通用组件缓冲区
    if (ctx->queue_buffer) free(ctx->queue_buffer);
    if (ctx->pool_memory_buffer) free(ctx->pool_memory_buffer);
    if (ctx->pool_free_list_buffer) free(ctx->pool_free_list_buffer);

    free(ctx);

    return VIDEO_OK;
}

// ==========================================================================
// 内部辅助函数实现（重构版）
// ==========================================================================

static frame_node_t* _frame_link_alloc_frame(frame_link_context_t *ctx)
{
    frame_node_t *node = NULL;
    PoolErr_t err = Pool_Acquire(&ctx->frame_pool, (void**)&node);
    
    if (err != POOL_OK || node == NULL) {
        return NULL;
    }
    
    // 清零节点
    memset(node, 0, sizeof(frame_node_t));
    return node;
}

static void _frame_link_free_frame(frame_link_context_t *ctx, frame_node_t *node)
{
    if (node == NULL) return;
    Pool_Release(&ctx->frame_pool, node);
}

static int _frame_link_enqueue(frame_link_context_t *ctx, frame_node_t *node)
{
    pthread_mutex_lock(&ctx->queue_lock);

    // 【重构】队列满：丢旧帧
    if (Queue_IsFull(&ctx->frame_queue)) {
        void *old_node = NULL;
        if (Queue_Get(&ctx->frame_queue, &old_node) == QUEUE_OK && old_node != NULL) {
            _frame_link_free_frame(ctx, (frame_node_t*)old_node);
        }
    }

    // 【重构】入队
    QueueErr_t qerr = Queue_Put(&ctx->frame_queue, node);
    if (qerr != QUEUE_OK) {
        pthread_mutex_unlock(&ctx->queue_lock);
        return -1;
    }

    // 通知等待者
    pthread_cond_signal(&ctx->queue_not_empty);

    // 回调
    if (ctx->frame_ready_cb != NULL) {
        ctx->frame_ready_cb(&node->frame, ctx->cb_user_data);
    }

    pthread_mutex_unlock(&ctx->queue_lock);
    return 0;
}

static frame_node_t* _frame_link_dequeue(frame_link_context_t *ctx, uint32_t timeout_ms)
{
    pthread_mutex_lock(&ctx->queue_lock);

    while (Queue_IsEmpty(&ctx->frame_queue) && ctx->running) {
        if (timeout_ms == 0) {
            pthread_cond_wait(&ctx->queue_not_empty, &ctx->queue_lock);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&ctx->queue_not_empty, &ctx->queue_lock, &ts);
        }
    }

    if (!ctx->running || Queue_IsEmpty(&ctx->frame_queue)) {
        pthread_mutex_unlock(&ctx->queue_lock);
        return NULL;
    }

    // 【重构】出队
    frame_node_t *node = NULL;
    Queue_Get(&ctx->frame_queue, (void**)&node);

    pthread_cond_signal(&ctx->queue_not_full);
    pthread_mutex_unlock(&ctx->queue_lock);

    return node;
}

// ==========================================================================
// 采集线程（保持不变）
// ==========================================================================
static void* _frame_link_capture_thread(void *arg)
{
    frame_link_context_t *ctx = (frame_link_context_t*)arg;
    LOG_I("Frame Link: Capture thread entered loop");

    static int frame_count = 0;
    struct pollfd fds[2];  // 监听：摄像头 + 退出管道

    // 初始化poll监听集合
    fds[0].fd = ctx->cam_fd;
    fds[0].events = POLLIN;
    fds[1].fd = ctx->exit_pipe_read_fd;
    fds[1].events = POLLIN;

    while (g_app_ctx.app_running && ctx->running) 
    {
        // poll超时缩短为10ms，极低延迟响应退出
        int poll_ret = poll(fds, 2, 10);

        // 全局退出标志（兜底，最高优先级）
        if (!g_app_ctx.app_running) {
            LOG_I("Frame Link: Capture thread global exit triggered");
            break;
        }

        // 退出管道事件（立即退出）
        if (fds[1].revents & POLLIN) {
            LOG_I("Frame Link: Capture thread received exit pipe signal");
            break;
        }

        // poll错误/超时
        if (poll_ret <= 0) {
            continue;
        }

        // 摄像头正常取帧
        if (fds[0].revents & POLLIN) {
            video_frame_t hal_frame = {0};
            video_err_t err = video_get_frame(ctx->hal_handle, &hal_frame);

            if (err != VIDEO_OK) {
                continue;
            }

            // 【重构】从池里分配节点
            frame_node_t *node = _frame_link_alloc_frame(ctx);
            if (node != NULL) {
                // 拷贝HAL帧数据到节点
                memcpy(&node->frame, &hal_frame, sizeof(video_frame_t));
                
                // 入队
                _frame_link_enqueue(ctx, node);
            }

            // 归还HAL层缓冲区
            video_put_frame(ctx->hal_handle, &hal_frame);

            if (++frame_count % 30 == 0) {
                LOG_D("Frame Link: Captured %d frames", frame_count);
            }
        }
    }

    LOG_I("Frame Link: Capture thread exited gracefully");
    return NULL;
}