// src/service/capture_srv/src/capture_srv.c
#include "capture_srv.h"
#include "frame_link.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// ==========================================================================
// 内部状态结构体（完全封装）
// ==========================================================================
typedef struct {
    // 链路层句柄（唯一依赖）
    frame_link_handle_t link_handle;
    capture_srv_config_t config;

    // 状态
    bool initialized;
    bool running;

    // 通用回调（总线层后续适配）
    capture_srv_event_cb_t event_cb;
    void *cb_user_data;

    // 锁
    pthread_mutex_t lock;
} capture_srv_context_t;

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static void _capture_srv_notify_event(capture_srv_context_t *ctx,
                                        capture_srv_event_type_t type,
                                        void *data,
                                        uint32_t data_len);
static uint64_t _capture_srv_get_timestamp_us(void);

// ==========================================================================
// 对外API实现
// ==========================================================================

int capture_srv_init(const capture_srv_config_t *config,
                     capture_srv_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    // 分配上下文
    capture_srv_context_t *ctx = (capture_srv_context_t*)malloc(sizeof(capture_srv_context_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(capture_srv_context_t));

    // 拷贝配置
    memcpy(&ctx->config, config, sizeof(capture_srv_config_t));

    // 初始化锁
    pthread_mutex_init(&ctx->lock, NULL);

    // 初始化链路层（唯一依赖）
    video_err_t err = frame_link_init(&ctx->config.link_config, &ctx->link_handle);
    if (err != VIDEO_OK) {
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }

    ctx->initialized = true;
    ctx->running = false;
    ctx->event_cb = NULL;
    ctx->cb_user_data = NULL;

    // 自动启动（如果配置了）
    if (ctx->config.auto_start) {
        capture_srv_start((capture_srv_handle_t)ctx);
    }

    *out_handle = (capture_srv_handle_t)ctx;
    return 0;
}

int capture_srv_start(capture_srv_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    if (!ctx->initialized || ctx->running) {
        pthread_mutex_unlock(&ctx->lock);
        return 0;
    }

    // 启动链路层
    video_err_t err = frame_link_start(ctx->link_handle);
    if (err != VIDEO_OK) {
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    ctx->running = true;
    pthread_mutex_unlock(&ctx->lock);

    // 通知事件（通过通用回调）
    _capture_srv_notify_event(ctx, CAPTURE_SRV_EVENT_STARTED, NULL, 0);

    return 0;
}

int capture_srv_stop(capture_srv_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    if (!ctx->initialized || !ctx->running) {
        pthread_mutex_unlock(&ctx->lock);
        return 0;
    }

    // 停止链路层
    frame_link_stop(ctx->link_handle);
    ctx->running = false;
    pthread_mutex_unlock(&ctx->lock);

    // 通知事件
    _capture_srv_notify_event(ctx, CAPTURE_SRV_EVENT_STOPPED, NULL, 0);

    return 0;
}

int capture_srv_get_frame(capture_srv_handle_t handle,
                          video_frame_t *frame,
                          uint32_t timeout_ms)
{
    if (handle == NULL || frame == NULL) {
        return -1;
    }
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;

    if (!ctx->initialized || !ctx->running) {
        return -1;
    }

    // 只通过链路层获取数据
    return frame_link_get_frame(ctx->link_handle, frame, timeout_ms);
}

int capture_srv_put_frame(capture_srv_handle_t handle,
                          const video_frame_t *frame)
{
    if (handle == NULL || frame == NULL) {
        return -1;
    }
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;

    if (!ctx->initialized) {
        return -1;
    }

    // 只通过链路层归还数据
    return frame_link_put_frame(ctx->link_handle, frame);
}

int capture_srv_register_event_cb(capture_srv_handle_t handle,
                                    capture_srv_event_cb_t cb,
                                    void *user_data)
{
    if (handle == NULL) {
        return -1;
    }
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    ctx->event_cb = cb;
    ctx->cb_user_data = user_data;
    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

int capture_srv_deinit(capture_srv_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    if (!ctx->initialized) {
        pthread_mutex_unlock(&ctx->lock);
        return 0;
    }

    // 先停止
    capture_srv_stop(handle);

    // 反初始化链路层
    frame_link_deinit(ctx->link_handle);

    ctx->initialized = false;
    pthread_mutex_unlock(&ctx->lock);

    // 释放资源
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);

    return 0;
}

// ==========================================================================
// 内部辅助函数实现
// ==========================================================================

static void _capture_srv_notify_event(capture_srv_context_t *ctx,
                                        capture_srv_event_type_t type,
                                        void *data,
                                        uint32_t data_len)
{
    if (ctx == NULL) return;

    pthread_mutex_lock(&ctx->lock);
    if (ctx->event_cb != NULL) {
        capture_srv_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = type;
        event.timestamp = _capture_srv_get_timestamp_us();
        event.data = data;
        event.data_len = data_len;

        // 调用回调（总线层后续在此处接入）
        ctx->event_cb(&event, ctx->cb_user_data);
    }
    pthread_mutex_unlock(&ctx->lock);
}

static uint64_t _capture_srv_get_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}