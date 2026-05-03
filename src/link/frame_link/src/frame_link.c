#include "frame_link.h"
#include "video_hal.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <sys/poll.h>
#include "log.h"
#include "main.h"   // 【新增】引入全局应用上下文

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define FRAME_LINK_MAX_POOL_SIZE 32
#define FRAME_LINK_MAX_QUEUE_SIZE 16

// ==========================================================================
// 内部帧节点结构体
// ==========================================================================
typedef struct {
    video_frame_t frame;       // 帧数据
    bool in_use;               // 是否被占用
    uint32_t ref_count;        // 引用计数（预留）
} frame_node_t;

// ==========================================================================
// 【核心】内部状态结构体（新增退出管道fd）
// ==========================================================================
typedef struct {
    // HAL层相关
    video_handle_t hal_handle;
    video_capability_t hal_cap;
    frame_link_config_t config;
    int cam_fd;                // 摄像头设备fd（用于poll）

    // 帧池管理
    frame_node_t *frame_pool;
    uint32_t pool_size;
    pthread_mutex_t pool_lock;

    // 队列管理
    frame_node_t **queue;
    uint32_t queue_size;
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t queue_count;
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_not_empty;
    pthread_cond_t queue_not_full;

    // 采集线程
    pthread_t capture_thread;
    bool running;
    bool streaming;

    // 【新增】全局优雅退出管道读端
    int exit_pipe_read_fd;

    // 【预留】Service层回调
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
// 对外API：初始化时注入退出管道fd
// ==========================================================================
video_err_t frame_link_init(const frame_link_config_t *config,
                            int exit_pipe_read_fd,  // 【新增】注入全局退出管道
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

    // 初始化锁
    pthread_mutex_init(&ctx->pool_lock, NULL);
    pthread_mutex_init(&ctx->queue_lock, NULL);
    pthread_cond_init(&ctx->queue_not_empty, NULL);
    pthread_cond_init(&ctx->queue_not_full, NULL);

    // 分配帧池
    ctx->pool_size = ctx->config.frame_pool_size;
    ctx->frame_pool = (frame_node_t*)malloc(ctx->pool_size * sizeof(frame_node_t));
    if (ctx->frame_pool == NULL) {
        free(ctx);
        return VIDEO_ERR_INVALID_PARAM;
    }
    memset(ctx->frame_pool, 0, ctx->pool_size * sizeof(frame_node_t));

    // 分配队列
    ctx->queue_size = ctx->config.queue_size;
    ctx->queue = (frame_node_t**)malloc(ctx->queue_size * sizeof(frame_node_t*));
    if (ctx->queue == NULL) {
        free(ctx->frame_pool);
        free(ctx);
        return VIDEO_ERR_INVALID_PARAM;
    }
    memset(ctx->queue, 0, ctx->queue_size * sizeof(frame_node_t*));

    // 初始化HAL层
    video_err_t err = video_open(&ctx->config.hal_config, &ctx->hal_cap, &ctx->hal_handle);
    if (err != VIDEO_OK) {
        free(ctx->queue);
        free(ctx->frame_pool);
        free(ctx);
        return err;
    }

    // 获取摄像头fd（用于Link层poll）
    ctx->cam_fd = video_get_wait_fd(ctx->hal_handle);
    if (ctx->cam_fd < 0) {
        video_close(ctx->hal_handle);
        free(ctx->queue);
        free(ctx->frame_pool);
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

    // 启动采集线程
    LOG_I("Frame Link: Creating capture thread...");
    ctx->running = true;
    if (pthread_create(&ctx->capture_thread, NULL, _frame_link_capture_thread, ctx) != 0) {
        LOG_E("Frame Link: Failed to create thread");
        video_stop_stream(ctx->hal_handle);
        ctx->streaming = false;
        ctx->running = false;
        return VIDEO_ERR_INVALID_PARAM;
    }
    LOG_I("Frame Link: Capture thread created");

    return VIDEO_OK;
}

// 【修复】线程停止函数：配合全局退出，确保线程退出
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
    // 唤醒所有条件变量 + 依赖全局管道退出，双重保险
    pthread_cond_broadcast(&ctx->queue_not_empty);
    pthread_cond_broadcast(&ctx->queue_not_full);
    // 等待线程安全退出
    pthread_join(ctx->capture_thread, NULL);

    // 停止HAL层流
    if (ctx->streaming) {
        video_stop_stream(ctx->hal_handle);
        ctx->streaming = false;
    }

    // 清空队列
    pthread_mutex_lock(&ctx->queue_lock);
    while (ctx->queue_count > 0) {
        frame_node_t *node = ctx->queue[ctx->queue_head];
        ctx->queue_head = (ctx->queue_head + 1) % ctx->queue_size;
        ctx->queue_count--;
        _frame_link_free_frame(ctx, node);
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
    return VIDEO_OK;
}

video_err_t frame_link_put_frame(frame_link_handle_t handle,
                                  const video_frame_t *frame)
{
    if (handle == NULL || frame == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    // 通过index找到对应的帧节点
    pthread_mutex_lock(&ctx->pool_lock);
    frame_node_t *node = NULL;
    for (uint32_t i = 0; i < ctx->pool_size; i++) {
        if (ctx->frame_pool[i].frame.index == frame->index) {
            node = &ctx->frame_pool[i];
            break;
        }
    }
    pthread_mutex_unlock(&ctx->pool_lock);

    if (node == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }

    // 归还HAL层缓冲区
    video_put_frame(ctx->hal_handle, frame);

    // 释放帧节点
    _frame_link_free_frame(ctx, node);
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
    pthread_mutex_destroy(&ctx->pool_lock);
    pthread_mutex_destroy(&ctx->queue_lock);
    pthread_cond_destroy(&ctx->queue_not_empty);
    pthread_cond_destroy(&ctx->queue_not_full);

    free(ctx->queue);
    free(ctx->frame_pool);
    free(ctx);

    return VIDEO_OK;
}

// ==========================================================================
// 内部辅助函数实现（无修改）
// ==========================================================================
static frame_node_t* _frame_link_alloc_frame(frame_link_context_t *ctx)
{
    pthread_mutex_lock(&ctx->pool_lock);
    frame_node_t *node = NULL;
    for (uint32_t i = 0; i < ctx->pool_size; i++) {
        if (!ctx->frame_pool[i].in_use) {
            node = &ctx->frame_pool[i];
            node->in_use = true;
            node->ref_count = 1;
            break;
        }
    }
    pthread_mutex_unlock(&ctx->pool_lock);
    return node;
}

static void _frame_link_free_frame(frame_link_context_t *ctx, frame_node_t *node)
{
    if (node == NULL) return;

    pthread_mutex_lock(&ctx->pool_lock);
    node->in_use = false;
    node->ref_count = 0;
    memset(&node->frame, 0, sizeof(video_frame_t));
    pthread_mutex_unlock(&ctx->pool_lock);
}

static int _frame_link_enqueue(frame_link_context_t *ctx, frame_node_t *node)
{
    pthread_mutex_lock(&ctx->queue_lock);

    // 队列满：丢旧帧
    if (ctx->queue_count >= ctx->queue_size) {
        frame_node_t *old_node = ctx->queue[ctx->queue_head];
        ctx->queue_head = (ctx->queue_head + 1) % ctx->queue_size;
        ctx->queue_count--;
        _frame_link_free_frame(ctx, old_node);
    }

    // 入队
    ctx->queue[ctx->queue_tail] = node;
    ctx->queue_tail = (ctx->queue_tail + 1) % ctx->queue_size;
    ctx->queue_count++;

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

    while (ctx->queue_count == 0 && ctx->running) {
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

    if (!ctx->running || ctx->queue_count == 0) {
        pthread_mutex_unlock(&ctx->queue_lock);
        return NULL;
    }

    // 出队
    frame_node_t *node = ctx->queue[ctx->queue_head];
    ctx->queue_head = (ctx->queue_head + 1) % ctx->queue_size;
    ctx->queue_count--;

    pthread_cond_signal(&ctx->queue_not_full);
    pthread_mutex_unlock(&ctx->queue_lock);

    return node;
}

// ==========================================================================
// 【核心修复】采集线程：统一全局退出标志 + 强化管道监听
// ==========================================================================
static void* _frame_link_capture_thread(void *arg)
{
    frame_link_context_t *ctx = (frame_link_context_t*)arg;
    LOG_I("Frame Link: Capture thread entered loop");

    static int frame_count = 0;
    struct pollfd fds[2];  // 监听两个fd：摄像头 + 退出管道

    // 初始化poll监听集合
    fds[0].fd = ctx->cam_fd;
    fds[0].events = POLLIN;
    fds[1].fd = ctx->exit_pipe_read_fd;
    fds[1].events = POLLIN;

    // 【核心修复】使用全局唯一退出标志，统一全系统退出
    while (g_app_ctx.app_running && ctx->running) {
        // poll超时100ms，避免死等，同时响应退出信号
        int poll_ret = poll(fds, 2, 100);

        // 1. 【最高优先级】全局退出标志检查（兜底）
        if (!g_app_ctx.app_running) {
            LOG_I("Frame Link: Capture thread global exit, quit immediately");
            break;
        }

        // 2. 检测全局退出管道事件
        if (fds[1].revents & POLLIN) {
            LOG_I("Frame Link: Capture thread receive exit pipe signal, quit immediately");
            break;
        }

        // 3. poll错误/超时
        if (poll_ret <= 0) {
            continue;
        }

        // 4. 摄像头有数据：正常取帧
        if (fds[0].revents & POLLIN) {
            video_frame_t hal_frame = {0};
            video_err_t err = video_get_frame(ctx->hal_handle, &hal_frame);

            if (err != VIDEO_OK) {
                continue;
            }

            // 帧就绪回调
            if (ctx->frame_ready_cb != NULL) {
                ctx->frame_ready_cb(&hal_frame, ctx->cb_user_data);
            }

            // 归还缓冲区
            video_put_frame(ctx->hal_handle, &hal_frame);

            // 保活日志
            if (++frame_count % 30 == 0) {
                LOG_D("Frame Link: Captured %d frames", frame_count);
            }
        }
    }

    LOG_I("Frame Link: Capture thread exited gracefully");
    return NULL;
}