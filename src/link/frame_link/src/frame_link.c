/* SPDX-License-Identifier: MIT */
/**
 * @file    frame_link.c
 * @brief   帧数据链路层实现（命名化多实例版）
 * @details 原子引用计数、静态内存池、线程安全、多实例隔离
 * @author  Luo
 * @date    2026-05-31
 */

#include "frame_link.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// ==========================================================================
// 【多实例管理】对标数据总线：静态实例表 + 名称管理
// ==========================================================================
typedef struct {
    char                    name[FRAME_LINK_NAME_MAX_LEN];
    frame_link_handle_t     handle;
    bool                    used;
} frame_link_entry_t;

static frame_link_entry_t s_frame_link_table[FRAME_LINK_MAX_INSTANCES] = {0};
static pthread_mutex_t    s_table_lock = PTHREAD_MUTEX_INITIALIZER;

// ==========================================================================
// 内部私有上下文（对外完全隐藏）
// ==========================================================================
typedef struct frame_link_t {
    Pool_t                  frame_pool;    /**< 帧内存池 */
    Queue_t                 frame_queue;   /**< 消费队列 */
    frame_link_config_t     config;        /**< 配置参数 */
    pthread_mutex_t         lock;          /**< 池操作互斥锁 */

    uint8_t*                pool_buffer;   /**< 内存池缓冲区 */
    void**                  free_list_buf; /**< 内存池空闲列表 */
    void**                  queue_buffer;  /**< 队列缓冲区 */
} frame_link_ctx_t;

// ==========================================================================
// 内部工具函数
// ==========================================================================
static uint64_t _get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static bool _frame_is_allocable(frame_t* frame) {
    return atomic_load(&frame->ref_cnt) == 0;
}

// 按名称查找帧链路实例
static frame_link_ctx_t* _find_ctx(const char* name) {
    if (!name) return NULL;
    pthread_mutex_lock(&s_table_lock);
    frame_link_ctx_t* ctx = NULL;
    for (int i = 0; i < FRAME_LINK_MAX_INSTANCES; i++) {
        if (s_frame_link_table[i].used && 
            strcmp(s_frame_link_table[i].name, name) == 0) {
            ctx = (frame_link_ctx_t*)s_frame_link_table[i].handle;
            break;
        }
    }
    pthread_mutex_unlock(&s_table_lock);
    return ctx;
}

// ==========================================================================
// 命名化初始化API
// ==========================================================================
int frame_link_init(const frame_link_config_t* config) {
    if (!config || !config->name || strlen(config->name) >= FRAME_LINK_NAME_MAX_LEN)
        return -1;

    // 重名检查
    if (_find_ctx(config->name)) {
        LOG_E("FrameLink[%s]: 已存在", config->name);
        return -1;
    }

    pthread_mutex_lock(&s_table_lock);
    // 查找空闲实例
    int free_idx = -1;
    for (int i = 0; i < FRAME_LINK_MAX_INSTANCES; i++) {
        if (!s_frame_link_table[i].used) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        pthread_mutex_unlock(&s_table_lock);
        LOG_E("FrameLink: 实例表已满");
        return -2;
    }

    // 分配上下文
    frame_link_ctx_t* ctx = calloc(1, sizeof(frame_link_ctx_t));
    if (!ctx) {
        pthread_mutex_unlock(&s_table_lock);
        return -3;
    }
    memcpy(&ctx->config, config, sizeof(frame_link_config_t));

    // 内存分配
    size_t item_size = sizeof(frame_t) + config->max_frame_size;
    ctx->pool_buffer = malloc(item_size * config->pool_capacity);
    ctx->free_list_buf = malloc(sizeof(void*) * config->pool_capacity);
    ctx->queue_buffer = malloc(sizeof(void*) * config->queue_capacity);
    if (!ctx->pool_buffer || !ctx->free_list_buf || !ctx->queue_buffer)
        goto err_free;

    // 初始化锁/池/队列
    pthread_mutex_init(&ctx->lock, NULL);
    Pool_Init(&ctx->frame_pool, item_size, config->pool_capacity,
              ctx->pool_buffer, ctx->free_list_buf);
    Queue_Init(&ctx->frame_queue, ctx->queue_buffer, config->queue_capacity);

    // 预初始化所有帧
    for (uint32_t i = 0; i < config->pool_capacity; i++) {
        frame_t* frame = (frame_t*)(ctx->pool_buffer + i * item_size);
        frame->data = (uint8_t*)frame + sizeof(frame_t);
        atomic_init(&frame->ref_cnt, 0);
    }

    // 注册实例
    strncpy(s_frame_link_table[free_idx].name, config->name, FRAME_LINK_NAME_MAX_LEN-1);
    s_frame_link_table[free_idx].handle = ctx;
    s_frame_link_table[free_idx].used = true;
    pthread_mutex_unlock(&s_table_lock);

    LOG_I("FrameLink[%s]: 初始化成功 | 池=%u 队列=%u",
          config->name, config->pool_capacity, config->queue_capacity);
    return 0;

err_free:
    free(ctx->pool_buffer);
    free(ctx->free_list_buf);
    free(ctx->queue_buffer);
    free(ctx);
    pthread_mutex_unlock(&s_table_lock);
    return -4;
}

// ==========================================================================
// 命名化销毁API
// ==========================================================================
int frame_link_deinit(const char* name) {
    frame_link_ctx_t* ctx = _find_ctx(name);
    if (!ctx) return 0;

    // 释放资源
    frame_link_clear_queue(name);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx->pool_buffer);
    free(ctx->free_list_buf);
    free(ctx->queue_buffer);
    free(ctx);

    // 清空实例表
    pthread_mutex_lock(&s_table_lock);
    for (int i = 0; i < FRAME_LINK_MAX_INSTANCES; i++) {
        if (s_frame_link_table[i].used && 
            strcmp(s_frame_link_table[i].name, name) == 0) {
            memset(&s_frame_link_table[i], 0, sizeof(frame_link_entry_t));
            break;
        }
    }
    pthread_mutex_unlock(&s_table_lock);

    LOG_I("FrameLink[%s]: 销毁成功", name);
    return 0;
}

frame_link_handle_t frame_link_get_handle(const char* name) {
    return (frame_link_handle_t)_find_ctx(name);
}

// ==========================================================================
// 生产者接口（完全保留原逻辑）
// ==========================================================================
int frame_link_get_free_frame(const char* name, frame_t** out_frame) {
    frame_link_ctx_t* ctx = _find_ctx(name);
    if (!ctx || !out_frame) return -1;

    void* item = NULL;
    pthread_mutex_lock(&ctx->lock);
    PoolErr_t ret = Pool_Acquire(&ctx->frame_pool, &item);
    if (ret != POOL_OK) {
        pthread_mutex_unlock(&ctx->lock);
        return ret;
    }

    frame_t* frame = (frame_t*)item;
    if (!_frame_is_allocable(frame)) {
        Pool_Release(&ctx->frame_pool, item);
        pthread_mutex_unlock(&ctx->lock);
        return -5;
    }

    memset(frame, 0, sizeof(frame_t));
    frame->data = (uint8_t*)frame + sizeof(frame_t);
    frame->timestamp = _get_timestamp_us();
    atomic_store(&frame->ref_cnt, 0);
    *out_frame = frame;

    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

int frame_link_enqueue_frame(const char* name, frame_t* frame) {
    frame_link_ctx_t* ctx = _find_ctx(name);
    if (!ctx || !frame) return -1;

    while (Queue_IsFull(&ctx->frame_queue)) {
        void* old = NULL;
        if (Queue_Get(&ctx->frame_queue, &old) == QUEUE_OK) {
            atomic_store(&((frame_t*)old)->ref_cnt, 0);
            Pool_Release(&ctx->frame_pool, old);
        }
    }
    return Queue_Put(&ctx->frame_queue, frame);
}

int frame_link_return_free_frame(const char* name, frame_t* frame) {
    frame_link_ctx_t* ctx = _find_ctx(name);
    if (!ctx || !frame) return -1;

    pthread_mutex_lock(&ctx->lock);
    atomic_store(&frame->ref_cnt, 0);
    Pool_Release(&ctx->frame_pool, frame);
    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

// ==========================================================================
// 消费者接口（完全保留原逻辑）
// ==========================================================================
int frame_link_dequeue_frame(const char* name, frame_t** out_frame) {
    frame_link_ctx_t* ctx = _find_ctx(name);
    if (!ctx || !out_frame) return -1;

    void* item = NULL;
    QueueErr_t ret = Queue_Get(&ctx->frame_queue, &item);
    *out_frame = (frame_t*)item;
    return ret;
}

int frame_link_ref_frame(const char* name, frame_t* frame) {
    (void)name;
    if (!frame) return -1;
    atomic_fetch_add(&frame->ref_cnt, 1);
    return 0;
}

int frame_link_unref_frame(const char* name, frame_t* frame) {
    frame_link_ctx_t* ctx = _find_ctx(name);
    if (!ctx || !frame) return -1;

    unsigned int new_cnt = atomic_fetch_sub(&frame->ref_cnt, 1) - 1;
    if (new_cnt == 0) {
        pthread_mutex_lock(&ctx->lock);
        Pool_Release(&ctx->frame_pool, frame);
        pthread_mutex_unlock(&ctx->lock);
    }
    return 0;
}

// ==========================================================================
// 监控接口
// ==========================================================================
uint32_t frame_link_get_queue_count(const char* name) {
    frame_link_ctx_t* ctx = _find_ctx(name);
    return ctx ? Queue_GetCount(&ctx->frame_queue) : 0;
}

int frame_link_clear_queue(const char* name) {
    frame_link_ctx_t* ctx = _find_ctx(name);
    if (!ctx) return 0;

    void* frame = NULL;
    while (Queue_Get(&ctx->frame_queue, &frame) == QUEUE_OK) {
        atomic_store(&((frame_t*)frame)->ref_cnt, 0);
        Pool_Release(&ctx->frame_pool, frame);
    }
    Queue_Clear(&ctx->frame_queue);
    return 0;
}

uint32_t frame_link_get_free_count(const char* name) {
    frame_link_ctx_t* ctx = _find_ctx(name);
    return ctx ? Pool_GetFreeCount(&ctx->frame_pool) : 0;
}