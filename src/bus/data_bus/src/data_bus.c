// src/bus/data_bus/src/data_bus.c
#include "data_bus.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stddef.h>
// ==========================================================================
// 内部宏定义
// ==========================================================================
#define MAX_FRAMES 128

// ==========================================================================
// 内部帧扩展结构体
// ==========================================================================
typedef struct {
    data_frame_t public_frame;    // 对外公开的帧
    data_free_callback_t free_cb; // 自定义释放回调
    void *free_user_data;
    bool in_use;
} internal_frame_t;

// ==========================================================================
// 内部上下文结构体
// ==========================================================================
typedef struct {
    internal_frame_t frames[MAX_FRAMES];
    uint32_t frame_count;
    // 队列（按类型）
    internal_frame_t *queue[DATA_TYPE_MAX][MAX_FRAMES];
    uint32_t queue_head[DATA_TYPE_MAX];
    uint32_t queue_tail[DATA_TYPE_MAX];
    uint32_t queue_count[DATA_TYPE_MAX];
    // 统计
    data_bus_stats_t stats;
    bool enable_stats;
    // 锁
    pthread_mutex_t lock;
    pthread_cond_t not_empty[DATA_TYPE_MAX];
} data_bus_context_t;

// ==========================================================================
// 数据类型字符串映射表
// ==========================================================================
static const char* g_data_type_str[] = {
    [DATA_TYPE_INVALID] = "INVALID",
    [DATA_TYPE_VIDEO_FRAME] = "VIDEO_FRAME",
    [DATA_TYPE_AUDIO_FRAME] = "AUDIO_FRAME",
    [DATA_TYPE_AI_RESULT] = "AI_RESULT",
    [DATA_TYPE_IMAGE] = "IMAGE",
};

// ==========================================================================
// 内部辅助函数
// ==========================================================================
static uint64_t _data_bus_get_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// ==========================================================================
// 对外API实现
// ==========================================================================

const char* data_type_to_str(data_type_t type)
{
    if (type < 0 || type >= DATA_TYPE_MAX) {
        return "CUSTOM";
    }
    return g_data_type_str[type];
}

int data_bus_init(const data_bus_config_t *config, data_bus_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return -1;
    }

    data_bus_context_t *ctx = (data_bus_context_t*)malloc(sizeof(data_bus_context_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(data_bus_context_t));

    ctx->enable_stats = config ? config->enable_stats : true;
    ctx->frame_count = config ? (config->max_frames > 0 ? config->max_frames : MAX_FRAMES) : MAX_FRAMES;

    // 初始化队列
    for (int i = 0; i < DATA_TYPE_MAX; i++) {
        ctx->queue_head[i] = 0;
        ctx->queue_tail[i] = 0;
        ctx->queue_count[i] = 0;
        pthread_cond_init(&ctx->not_empty[i], NULL);
    }

    pthread_mutex_init(&ctx->lock, NULL);

    *out_handle = (data_bus_handle_t)ctx;
    return 0;
}

int data_bus_alloc_frame(data_bus_handle_t handle,
                         data_type_t type,
                         uint32_t data_len,
                         data_frame_t **out_frame)
{
    if (handle == NULL || out_frame == NULL || type == DATA_TYPE_INVALID) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    // 找空闲帧
    internal_frame_t *int_frame = NULL;
    for (uint32_t i = 0; i < ctx->frame_count; i++) {
        if (!ctx->frames[i].in_use) {
            int_frame = &ctx->frames[i];
            break;
        }
    }

    if (int_frame == NULL) {
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }

    // 分配数据内存
    void *data = NULL;
    if (data_len > 0) {
        data = malloc(data_len);
        if (data == NULL) {
            pthread_mutex_unlock(&ctx->lock);
            return -1;
        }
    }

    // 初始化帧
    memset(int_frame, 0, sizeof(internal_frame_t));
    int_frame->in_use = true;
    int_frame->public_frame.type = type;
    int_frame->public_frame.timestamp = _data_bus_get_timestamp_us();
    int_frame->public_frame.data = data;
    int_frame->public_frame.data_len = data_len;
    int_frame->public_frame.ref_count = 1;

    *out_frame = &int_frame->public_frame;
    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

int data_bus_publish(data_bus_handle_t handle, data_frame_t *frame)
{
    if (handle == NULL || frame == NULL) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    // 找到内部帧
    internal_frame_t *int_frame = (internal_frame_t*)((uint8_t*)frame - offsetof(internal_frame_t, public_frame));

    pthread_mutex_lock(&ctx->lock);

    // 入队
    data_type_t type = frame->type;
    if (ctx->queue_count[type] >= MAX_FRAMES) {
        // 队列满，丢最旧的帧
        internal_frame_t *old_frame = ctx->queue[type][ctx->queue_head[type]];
        ctx->queue_head[type] = (ctx->queue_head[type] + 1) % MAX_FRAMES;
        ctx->queue_count[type]--;
        // 释放旧帧
        if (old_frame->public_frame.ref_count > 0) {
            old_frame->public_frame.ref_count--;
            if (old_frame->public_frame.ref_count == 0) {
                if (old_frame->free_cb) {
                    old_frame->free_cb((void*)old_frame->public_frame.data, old_frame->free_user_data);
                } else {
                    free((void*)old_frame->public_frame.data);
                }
                old_frame->in_use = false;
            }
        }
    }

    ctx->queue[type][ctx->queue_tail[type]] = int_frame;
    ctx->queue_tail[type] = (ctx->queue_tail[type] + 1) % MAX_FRAMES;
    ctx->queue_count[type]++;

    // 更新统计
    if (ctx->enable_stats) {
        ctx->stats.total_published++;
        if (type < DATA_TYPE_MAX) {
            ctx->stats.frame_count[type]++;
        }
    }

    // 通知等待者
    pthread_cond_signal(&ctx->not_empty[type]);
    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

int data_bus_acquire(data_bus_handle_t handle,
                     data_type_t type,
                     data_frame_t **out_frame,
                     uint32_t timeout_ms)
{
    if (handle == NULL || out_frame == NULL || type == DATA_TYPE_INVALID) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    // 等待队列非空
    while (ctx->queue_count[type] == 0) {
        if (timeout_ms == 0) {
            pthread_cond_wait(&ctx->not_empty[type], &ctx->lock);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            int ret = pthread_cond_timedwait(&ctx->not_empty[type], &ctx->lock, &ts);
            if (ret != 0) {
                pthread_mutex_unlock(&ctx->lock);
                return -1;
            }
        }
    }

    // 出队
    internal_frame_t *int_frame = ctx->queue[type][ctx->queue_head[type]];
    ctx->queue_head[type] = (ctx->queue_head[type] + 1) % MAX_FRAMES;
    ctx->queue_count[type]--;

    // 引用计数+1
    int_frame->public_frame.ref_count++;

    // 更新统计
    if (ctx->enable_stats) {
        ctx->stats.total_acquired++;
    }

    *out_frame = &int_frame->public_frame;
    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

int data_bus_release(data_bus_handle_t handle, data_frame_t *frame)
{
    if (handle == NULL || frame == NULL) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    // 找到内部帧
    internal_frame_t *int_frame = (internal_frame_t*)((uint8_t*)frame - offsetof(internal_frame_t, public_frame));

    pthread_mutex_lock(&ctx->lock);

    // 引用计数-1
    int_frame->public_frame.ref_count--;

    // 更新统计
    if (ctx->enable_stats) {
        ctx->stats.total_released++;
    }

    // 引用计数为0，释放
    if (int_frame->public_frame.ref_count == 0) {
        if (int_frame->free_cb) {
            int_frame->free_cb((void*)int_frame->public_frame.data, int_frame->free_user_data);
        } else {
            free((void*)int_frame->public_frame.data);
        }
        int_frame->in_use = false;
    }

    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

int data_bus_set_free_callback(data_bus_handle_t handle,
                                data_frame_t *frame,
                                data_free_callback_t cb,
                                void *user_data)
{
    if (handle == NULL || frame == NULL) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    internal_frame_t *int_frame = (internal_frame_t*)((uint8_t*)frame - offsetof(internal_frame_t, public_frame));

    pthread_mutex_lock(&ctx->lock);
    int_frame->free_cb = cb;
    int_frame->free_user_data = user_data;
    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

int data_bus_get_stats(data_bus_handle_t handle, data_bus_stats_t *stats)
{
    if (handle == NULL || stats == NULL) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    memcpy(stats, &ctx->stats, sizeof(data_bus_stats_t));
    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

int data_bus_reset_stats(data_bus_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    pthread_mutex_unlock(&ctx->lock);

    return 0;
}

int data_bus_deinit(data_bus_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    data_bus_context_t *ctx = (data_bus_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);
    // 释放所有帧
    for (uint32_t i = 0; i < ctx->frame_count; i++) {
        if (ctx->frames[i].in_use) {
            if (ctx->frames[i].free_cb) {
                ctx->frames[i].free_cb((void*)ctx->frames[i].public_frame.data, ctx->frames[i].free_user_data);
            } else {
                free((void*)ctx->frames[i].public_frame.data);
            }
            ctx->frames[i].in_use = false;
        }
    }
    pthread_mutex_unlock(&ctx->lock);

    for (int i = 0; i < DATA_TYPE_MAX; i++) {
        pthread_cond_destroy(&ctx->not_empty[i]);
    }
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    return 0;
}
