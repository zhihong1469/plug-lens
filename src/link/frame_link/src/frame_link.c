#include "frame_link.h"
#include "video_hal.h"
#include "pool.h"
#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/poll.h>
#include "log.h"
#include "main.h"

#define FRAME_LINK_MAX_POOL_SIZE 32
#define FRAME_LINK_MAX_QUEUE_SIZE 16

typedef struct {
    video_frame_t frame;
} frame_node_t;

typedef struct {
    video_handle_t hal_handle;
    video_capability_t hal_cap;
    frame_link_config_t config;
    int cam_fd;

    Pool_t frame_pool;
    void *pool_memory_buffer;
    void **pool_free_list_buffer;

    Queue_t frame_queue;
    void **queue_buffer;

    pthread_mutex_t queue_lock;
    pthread_cond_t queue_not_empty;
    pthread_cond_t queue_not_full;

    bool streaming;
    int exit_pipe_read_fd;
} frame_link_context_t;

static frame_node_t* _frame_link_alloc_frame(frame_link_context_t *ctx);
static void _frame_link_free_frame(frame_link_context_t *ctx, frame_node_t *node);
static int _frame_link_enqueue(frame_link_context_t *ctx, frame_node_t *node);
static frame_node_t* _frame_link_dequeue(frame_link_context_t *ctx, uint32_t timeout_ms);
static int _frame_link_poll_and_capture(frame_link_context_t *ctx, uint32_t timeout_ms);

video_err_t frame_link_init(const frame_link_config_t *config,
                            int exit_pipe_read_fd,
                            frame_link_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL || exit_pipe_read_fd < 0) {
        return VIDEO_ERR_INVALID_PARAM;
    }

    frame_link_context_t *ctx = (frame_link_context_t*)malloc(sizeof(frame_link_context_t));
    if (ctx == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    memset(ctx, 0, sizeof(frame_link_context_t));

    memcpy(&ctx->config, config, sizeof(frame_link_config_t));
    ctx->exit_pipe_read_fd = exit_pipe_read_fd;

    if (ctx->config.frame_pool_size == 0) ctx->config.frame_pool_size = 8;
    if (ctx->config.frame_pool_size > FRAME_LINK_MAX_POOL_SIZE) ctx->config.frame_pool_size = FRAME_LINK_MAX_POOL_SIZE;
    if (ctx->config.queue_size == 0) ctx->config.queue_size = 4;
    if (ctx->config.queue_size > FRAME_LINK_MAX_QUEUE_SIZE) ctx->config.queue_size = FRAME_LINK_MAX_QUEUE_SIZE;

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

    size_t queue_buf_size = sizeof(void*) * (ctx->config.queue_size + 1);
    ctx->queue_buffer = malloc(queue_buf_size);
    
    if (ctx->queue_buffer == NULL) {
        free(ctx->pool_memory_buffer);
        free(ctx->pool_free_list_buffer);
        free(ctx);
        return VIDEO_ERR_INVALID_PARAM;
    }
    
    Queue_Init(&ctx->frame_queue, ctx->queue_buffer, ctx->config.queue_size + 1);

    pthread_mutex_init(&ctx->queue_lock, NULL);
    pthread_cond_init(&ctx->queue_not_empty, NULL);
    pthread_cond_init(&ctx->queue_not_full, NULL);

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

    ctx->streaming = false;
    *out_handle = (frame_link_handle_t)ctx;
    return VIDEO_OK;
}

video_err_t frame_link_start_stream(frame_link_handle_t handle)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    if (ctx->streaming) {
        LOG_I("Frame Link: HAL stream already running");
        return VIDEO_OK;
    }

    LOG_I("Frame Link: Starting HAL layer stream...");
    video_err_t err = video_start_stream(ctx->hal_handle);
    if (err != VIDEO_OK) {
        LOG_E("Frame Link: Failed to start HAL stream (err=%d)", err);
        return err;
    }
    
    ctx->streaming = true;
    LOG_I("Frame Link: HAL stream started");
    return VIDEO_OK;
}

video_err_t frame_link_stop_stream(frame_link_handle_t handle)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    if (!ctx->streaming) {
        return VIDEO_OK;
    }

    LOG_I("Frame Link: Stopping HAL layer stream...");
    video_err_t err = video_stop_stream(ctx->hal_handle);
    if (err == VIDEO_OK) {
        ctx->streaming = false;
    }

    pthread_mutex_lock(&ctx->queue_lock);
    void *item = NULL;
    while (Queue_Get(&ctx->frame_queue, &item) == QUEUE_OK) {
        if (item != NULL) {
            _frame_link_free_frame(ctx, (frame_node_t*)item);
        }
    }
    pthread_mutex_unlock(&ctx->queue_lock);

    LOG_I("Frame Link: HAL stream stopped");
    return err;
}

video_err_t frame_link_get_frame(frame_link_handle_t handle,
                                  video_frame_t *frame,
                                  uint32_t timeout_ms)
{
    if (handle == NULL || frame == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    _frame_link_poll_and_capture(ctx, timeout_ms > 0 ? timeout_ms : 10);

    frame_node_t *node = _frame_link_dequeue(ctx, timeout_ms);
    if (node == NULL) {
        return VIDEO_ERR_POLL;
    }

    memcpy(frame, &node->frame, sizeof(video_frame_t));
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

    video_put_frame(ctx->hal_handle, frame);
    
    return VIDEO_OK;
}

video_err_t frame_link_deinit(frame_link_handle_t handle)
{
    if (handle == NULL) {
        return VIDEO_ERR_INVALID_PARAM;
    }
    frame_link_context_t *ctx = (frame_link_context_t*)handle;

    frame_link_stop_stream(handle);
    video_close(ctx->hal_handle);

    pthread_mutex_destroy(&ctx->queue_lock);
    pthread_cond_destroy(&ctx->queue_not_empty);
    pthread_cond_destroy(&ctx->queue_not_full);

    if (ctx->queue_buffer) free(ctx->queue_buffer);
    if (ctx->pool_memory_buffer) free(ctx->pool_memory_buffer);
    if (ctx->pool_free_list_buffer) free(ctx->pool_free_list_buffer);

    free(ctx);
    LOG_I("Frame Link: Deinitialized");
    return VIDEO_OK;
}

static frame_node_t* _frame_link_alloc_frame(frame_link_context_t *ctx)
{
    frame_node_t *node = NULL;
    PoolErr_t err = Pool_Acquire(&ctx->frame_pool, (void**)&node);
    
    if (err != POOL_OK || node == NULL) {
        return NULL;
    }
    
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

    if (Queue_IsFull(&ctx->frame_queue)) {
        void *old_node = NULL;
        if (Queue_Get(&ctx->frame_queue, &old_node) == QUEUE_OK && old_node != NULL) {
            _frame_link_free_frame(ctx, (frame_node_t*)old_node);
        }
    }

    QueueErr_t qerr = Queue_Put(&ctx->frame_queue, node);
    if (qerr != QUEUE_OK) {
        pthread_mutex_unlock(&ctx->queue_lock);
        return -1;
    }

    pthread_cond_signal(&ctx->queue_not_empty);
    pthread_mutex_unlock(&ctx->queue_lock);
    return 0;
}

static frame_node_t* _frame_link_dequeue(frame_link_context_t *ctx, uint32_t timeout_ms)
{
    pthread_mutex_lock(&ctx->queue_lock);

    while (Queue_IsEmpty(&ctx->frame_queue)) {
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

    frame_node_t *node = NULL;
    Queue_Get(&ctx->frame_queue, (void**)&node);

    pthread_cond_signal(&ctx->queue_not_full);
    pthread_mutex_unlock(&ctx->queue_lock);

    return node;
}

// 【核心修复】已删除内部的 video_put_frame，避免双重释放
static int _frame_link_poll_and_capture(frame_link_context_t *ctx, uint32_t timeout_ms)
{
    static int frame_count = 0;
    struct pollfd fds[2];

    fds[0].fd = ctx->cam_fd;
    fds[0].events = POLLIN;
    fds[1].fd = ctx->exit_pipe_read_fd;
    fds[1].events = POLLIN;

    int poll_ret = poll(fds, 2, timeout_ms);

    if (!g_app_ctx.app_running || (fds[1].revents & POLLIN)) {
        return -1;
    }

    if (poll_ret <= 0) {
        return 0;
    }

    if (fds[0].revents & POLLIN) {
        video_frame_t hal_frame = {0};
        video_err_t err = video_get_frame(ctx->hal_handle, &hal_frame);

        if (err != VIDEO_OK) {
            return 0;
        }

        frame_node_t *node = _frame_link_alloc_frame(ctx);
        if (node != NULL) {
            memcpy(&node->frame, &hal_frame, sizeof(video_frame_t));
            _frame_link_enqueue(ctx, node);
        }

        // ====================== 已彻底删除此处的 video_put_frame ======================

        if (++frame_count % 30 == 0) {
            LOG_D("Frame Link: Captured %d frames", frame_count);
        }
    }

    return 0;
}