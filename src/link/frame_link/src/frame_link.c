#include "../inc/frame_link.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ==========================================================================
// 内部私有结构体（对外完全隐藏）
// ==========================================================================
typedef struct frame_link_t{
    Pool_t          frame_pool;     // 帧内存池
    Queue_t         frame_queue;    // 帧队列
    frame_link_config_t config;     // 配置

    // 内部缓冲区（静态内存，无运行时malloc）
    uint8_t*        pool_buffer;    // 内存池数据缓冲区
    void**          free_list_buf;  // 内存池空闲列表
    void**          queue_buffer;   // 队列缓冲区
} frame_link_ctx_t;

// ==========================================================================
// 工具函数：获取当前微秒时间戳
// ==========================================================================
static uint64_t _get_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// ==========================================================================
// 初始化实现
// ==========================================================================
int frame_link_init(const frame_link_config_t* config, frame_link_handle_t* out_handle)
{
    if (!config || !out_handle || config->max_frame_size == 0 ||
        config->pool_capacity == 0 || config->queue_capacity == 0) {
        LOG_E("FrameLink: 无效参数");
        return -1;
    }

    // 分配上下文
    frame_link_ctx_t* ctx = (frame_link_ctx_t*)calloc(1, sizeof(frame_link_ctx_t));
    if (!ctx) {
        LOG_E("FrameLink: 分配上下文失败");
        return -2;
    }

    memcpy(&ctx->config, config, sizeof(frame_link_config_t));

    // 计算所需内存大小
    size_t pool_item_size = sizeof(frame_t) + config->max_frame_size;
    size_t pool_total_size = pool_item_size * config->pool_capacity;
    size_t free_list_size = sizeof(void*) * config->pool_capacity;
    size_t queue_size = sizeof(void*) * config->queue_capacity;

    // 分配静态缓冲区（一次性分配，无后续malloc）
    ctx->pool_buffer = (uint8_t*)malloc(pool_total_size);
    ctx->free_list_buf = (void**)malloc(free_list_size);
    ctx->queue_buffer = (void**)malloc(queue_size);

    if (!ctx->pool_buffer || !ctx->free_list_buf || !ctx->queue_buffer) {
        LOG_E("FrameLink: 分配缓冲区失败");
        goto err_free;
    }

    // 初始化内存池
    Pool_Init(&ctx->frame_pool,
              pool_item_size,
              config->pool_capacity,
              ctx->pool_buffer,
              ctx->free_list_buf);

    // 初始化队列
    Queue_Init(&ctx->frame_queue,
               ctx->queue_buffer,
               config->queue_capacity);

    // 预初始化所有帧的data指针（零拷贝优化）
    for (uint32_t i = 0; i < config->pool_capacity; i++) {
        frame_t* frame = (frame_t*)(ctx->pool_buffer + i * pool_item_size);
        frame->data = (uint8_t*)frame + sizeof(frame_t);
    }

    *out_handle = ctx;
    LOG_I("FrameLink: 初始化成功，池大小=%u，队列大小=%u，单帧最大=%zu字节",
          config->pool_capacity, config->queue_capacity, pool_item_size);
    return 0;

err_free:
    if (ctx->pool_buffer) free(ctx->pool_buffer);
    if (ctx->free_list_buf) free(ctx->free_list_buf);
    if (ctx->queue_buffer) free(ctx->queue_buffer);
    free(ctx);
    return -3;
}

// ==========================================================================
// 销毁实现
// ==========================================================================
int frame_link_deinit(frame_link_handle_t handle)
{
    if (!handle) return 0;

    frame_link_ctx_t* ctx = (frame_link_ctx_t*)handle;

    // 清空队列，归还所有帧
    frame_link_clear_queue(handle);

    // 释放缓冲区
    free(ctx->pool_buffer);
    free(ctx->free_list_buf);
    free(ctx->queue_buffer);
    free(ctx);

    LOG_I("FrameLink: 销毁完成");
    return 0;
}

// ==========================================================================
// 生产者接口实现
// ==========================================================================
int frame_link_get_free_frame(frame_link_handle_t handle, frame_t** out_frame)
{
    if (!handle || !out_frame) return -1;

    frame_link_ctx_t* ctx = (frame_link_ctx_t*)handle;
    void* item = NULL;

    PoolErr_t ret = Pool_Acquire(&ctx->frame_pool, &item);
    if (ret != POOL_OK) {
        LOG_D("FrameLink: 内存池空");
        return ret;
    }

    // 清零帧头（数据区不清零，提高性能）
    frame_t* frame = (frame_t*)item;
    memset(frame, 0, sizeof(frame_t));
    frame->data = (uint8_t*)frame + sizeof(frame_t); // 预计算数据指针
    frame->timestamp = _get_timestamp_us(); // 自动填充时间戳

    *out_frame = frame;
    return 0;
}

int frame_link_enqueue_frame(frame_link_handle_t handle, frame_t* frame)
{
    if (!handle || !frame) return -1;

    frame_link_ctx_t* ctx = (frame_link_ctx_t*)handle;

    // 【核心：丢旧保新策略】队列满时丢弃最旧帧
    while (Queue_IsFull(&ctx->frame_queue)) {
        void* old_frame = NULL;
        if (Queue_Get(&ctx->frame_queue, &old_frame) == QUEUE_OK) {
            Pool_Release(&ctx->frame_pool, old_frame);
            LOG_D("FrameLink: 队列满，丢弃旧帧");
        }
    }

    // 入队新帧
    QueueErr_t ret = Queue_Put(&ctx->frame_queue, frame);
    if (ret != QUEUE_OK) {
        LOG_E("FrameLink: 入队失败");
        Pool_Release(&ctx->frame_pool, frame);
        return ret;
    }

    return 0;
}

int frame_link_return_free_frame(frame_link_handle_t handle, frame_t* frame)
{
    if (!handle || !frame) return -1;

    frame_link_ctx_t* ctx = (frame_link_ctx_t*)handle;
    return Pool_Release(&ctx->frame_pool, frame);
}

// ==========================================================================
// 消费者接口实现
// ==========================================================================
int frame_link_dequeue_frame(frame_link_handle_t handle, frame_t** out_frame)
{
    if (!handle || !out_frame) return -1;

    frame_link_ctx_t* ctx = (frame_link_ctx_t*)handle;
    void* item = NULL;

    QueueErr_t ret = Queue_Get(&ctx->frame_queue, &item);
    if (ret != QUEUE_OK) {
        return ret;
    }

    *out_frame = (frame_t*)item;
    return 0;
}

int frame_link_release_frame(frame_link_handle_t handle, frame_t* frame)
{
    if (!handle || !frame) return -1;

    frame_link_ctx_t* ctx = (frame_link_ctx_t*)handle;
    return Pool_Release(&ctx->frame_pool, frame);
}

// ==========================================================================
// 管理接口实现
// ==========================================================================
uint32_t frame_link_get_queue_count(frame_link_handle_t handle)
{
    if (!handle) return 0;
    frame_link_ctx_t* ctx = (frame_link_ctx_t*)handle;
    return Queue_GetCount(&ctx->frame_queue);
}

int frame_link_clear_queue(frame_link_handle_t handle)
{
    if (!handle) return 0;

    frame_link_ctx_t* ctx = (frame_link_ctx_t*)handle;
    void* frame = NULL;

    // 取出所有帧并归还到内存池
    while (Queue_Get(&ctx->frame_queue, &frame) == QUEUE_OK) {
        Pool_Release(&ctx->frame_pool, frame);
    }

    Queue_Clear(&ctx->frame_queue);
    LOG_I("FrameLink: 队列已清空");
    return 0;
}

uint32_t frame_link_get_free_count(frame_link_handle_t handle)
{
    if (!handle) return 0;
    frame_link_ctx_t* ctx = (frame_link_ctx_t*)handle;
    return Pool_GetFreeCount(&ctx->frame_pool);
}