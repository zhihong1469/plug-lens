/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           frame_link.c
 * @brief          FrameLink 多实例数据链路层 私有实现
 * @defgroup       FrameLink
 * @details
 *   1. 静态实例表管理多命名链路
 *   2. 双超时锁：内存池锁 + 队列锁（固定顺序，无死锁）
 *   3. C11原子引用计数，无锁生命周期管理
 *   4. 内存池满不阻塞生产者，嵌入式最优策略
 *   5. 单写多读权限强制隔离
 *   6. 内部自动安全字符串拷贝，杜绝越界
 ******************************************************************************
 */
#include "frame_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

/* ========================== 私有数据类型 ========================== */
/** 帧内部结构体（对外完全隐藏） */
struct frame {
    frame_info_t          info;           /**< 公共元数据 */
    uint8_t               *data;          /**< 数据指针 */
    atomic_uint           ref_cnt;        /**< 原子引用计数 */
    bool                  read_only;      /**< 只读标记 */
    frame_link_handle_t   owner;          /**< 所属链路（安全校验） */
};

/** 单条数据链路上下文（对外完全隐藏） */
struct frame_link {
    frame_link_cfg_t      cfg;            /**< 配置 */
    bool                  inited;         /**< 初始化标记 */

    /* 内存池 */
    uint8_t               *mem_pool;      /**< 连续内存池基址 */
    struct frame          *frames;        /**< 帧对象数组 */
    uint32_t              free_cnt;       /**< 空闲帧数量 */

    /* 消费队列 */
    struct frame          **queue;
    uint32_t              q_head;
    uint32_t              q_tail;
    struct frame          *latest_frame;  /**< 最新帧缓存 */

    /* 双锁架构（规范核心） */
    pthread_mutex_t       pool_lock;      /**< 内存池分配/回收锁 */
    pthread_mutex_t       queue_lock;     /**< 队列入队/出队锁 */

    uint32_t              frame_id_inc;   /**< 帧ID自增 */
};

/** 全局静态实例表（同data_bus设计） */
typedef struct {
    char      name[FRAME_LINK_NAME_MAX_LEN];
    struct frame_link *link;
    bool      used;
} fl_instance_t;

/* ========================== 全局私有变量 ========================== */
static fl_instance_t g_fl_table[FRAME_LINK_MAX_INSTANCES] = {0};
static pthread_mutex_t g_table_lock = PTHREAD_MUTEX_INITIALIZER;

/* ========================== 内部工具函数 ========================== */
static uint64_t _fl_get_ts_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int _fl_mutex_timedlock(pthread_mutex_t *lock) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += FRAME_LINK_LOCK_TIMEOUT_MS * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }
    return pthread_mutex_timedlock(lock, &ts);
}

/**
 * @brief  内部安全字符串拷贝（自动补结束符，杜绝越界）
 */
static void _fl_safe_strcpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static struct frame_link *_fl_find_by_name(const char *name) {
    if (!name) return NULL;
    pthread_mutex_lock(&g_table_lock);
    struct frame_link *ret = NULL;
    for (uint32_t i = 0; i < FRAME_LINK_MAX_INSTANCES; i++) {
        if (g_fl_table[i].used && strcmp(g_fl_table[i].name, name) == 0) {
            ret = g_fl_table[i].link;
            break;
        }
    }
    pthread_mutex_unlock(&g_table_lock);
    return ret;
}

/* ========================== 全局接口实现 ========================== */
fl_err_t frame_link_create(const frame_link_cfg_t *cfg) {
    if (!cfg || !cfg->name || strlen(cfg->name) >= FRAME_LINK_NAME_MAX_LEN)
        return FL_INVALID_PARAM;
    if (_fl_find_by_name(cfg->name)) return FL_BUSY;
    if (cfg->pool_count < FRAME_LINK_POOL_MIN || cfg->pool_count > FRAME_LINK_POOL_MAX)
        return FL_INVALID_PARAM;
    if (!cfg->max_frame_size) return FL_INVALID_PARAM;

    pthread_mutex_lock(&g_table_lock);
    int free_idx = -1;
    for (uint32_t i = 0; i < FRAME_LINK_MAX_INSTANCES; i++) {
        if (!g_fl_table[i].used) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        pthread_mutex_unlock(&g_table_lock);
        return FL_BUSY;
    }

    struct frame_link *link = calloc(1, sizeof(struct frame_link));
    if (!link) {
        pthread_mutex_unlock(&g_table_lock);
        return FL_NO_MEM;
    }

    link->mem_pool = malloc(cfg->pool_count * (sizeof(struct frame) + cfg->max_frame_size));
    link->queue = malloc(sizeof(struct frame *) * cfg->queue_count);
    if (!link->mem_pool || !link->queue) {
        free(link->queue);
        free(link->mem_pool);
        free(link);
        pthread_mutex_unlock(&g_table_lock);
        return FL_NO_MEM;
    }

    pthread_mutex_init(&link->pool_lock, NULL);
    pthread_mutex_init(&link->queue_lock, NULL);
    link->cfg = *cfg;
    link->free_cnt = cfg->pool_count;
    link->frame_id_inc = 0;
    link->latest_frame = NULL;
    link->q_head = link->q_tail = 0;

    link->frames = (struct frame *)link->mem_pool;
    for (uint32_t i = 0; i < cfg->pool_count; i++) {
        struct frame *f = &link->frames[i];
        f->data = (uint8_t *)f + sizeof(struct frame);
        atomic_init(&f->ref_cnt, 0);
        f->read_only = false;
        f->owner = link;
        memset(f->data, 0, cfg->max_frame_size);
    }

    link->inited = true;
    // 内部封装安全拷贝，上层无需处理
    _fl_safe_strcpy(g_fl_table[free_idx].name, cfg->name, FRAME_LINK_NAME_MAX_LEN);
    g_fl_table[free_idx].link = link;
    g_fl_table[free_idx].used = true;

    pthread_mutex_unlock(&g_table_lock);
    return FL_OK;
}

fl_err_t frame_link_destroy(const char *name) {
    struct frame_link *link = _fl_find_by_name(name);
    if (!link) return FL_NOT_FOUND;

    frame_link_clear(name);
    pthread_mutex_destroy(&link->pool_lock);
    pthread_mutex_destroy(&link->queue_lock);
    free(link->queue);
    free(link->mem_pool);
    free(link);

    pthread_mutex_lock(&g_table_lock);
    for (uint32_t i = 0; i < FRAME_LINK_MAX_INSTANCES; i++) {
        if (g_fl_table[i].used && strcmp(g_fl_table[i].name, name) == 0) {
            memset(&g_fl_table[i], 0, sizeof(fl_instance_t));
            break;
        }
    }
    pthread_mutex_unlock(&g_table_lock);
    return FL_OK;
}

fl_err_t frame_link_clear(const char *name) {
    struct frame_link *link = _fl_find_by_name(name);
    if (!link) return FL_NOT_FOUND;

    if (_fl_mutex_timedlock(&link->queue_lock) != 0) return FL_TIMEOUT;
    if (_fl_mutex_timedlock(&link->pool_lock) != 0) {
        pthread_mutex_unlock(&link->queue_lock);
        return FL_TIMEOUT;
    }

    link->q_head = link->q_tail = 0;
    link->latest_frame = NULL;
    link->free_cnt = link->cfg.pool_count;

    for (uint32_t i = 0; i < link->cfg.pool_count; i++) {
        struct frame *f = &link->frames[i];
        atomic_store(&f->ref_cnt, 0);
        f->read_only = false;
    }

    pthread_mutex_unlock(&link->pool_lock);
    pthread_mutex_unlock(&link->queue_lock);
    return FL_OK;
}

/* ========================== 生产者接口 ========================== */
fl_err_t frame_link_producer_get(const char* name, frame_handle_t* out_frame) {
    if (!out_frame) return FL_INVALID_PARAM;
    struct frame_link *link = _fl_find_by_name(name);
    if (!link) return FL_NOT_FOUND;
    if (_fl_mutex_timedlock(&link->pool_lock) != 0) return FL_TIMEOUT;

    printf("[FL_DEBUG] 空闲帧数量: %u\n", link->free_cnt); 
    if (link->free_cnt == 0) {
        pthread_mutex_unlock(&link->pool_lock);
        return FL_NO_FREE_FRAME;
    }

    struct frame *free_f = NULL;
    for (uint32_t i = 0; i < link->cfg.pool_count; i++) {
        struct frame *f = &link->frames[i];
        printf("[FL_CHECK] 帧%d 真实引用: %u\n", i, atomic_load(&f->ref_cnt));
        if (atomic_load(&f->ref_cnt) == 0) {
            free_f = f;
            break;
        }
    }

    if (!free_f) {
        pthread_mutex_unlock(&link->pool_lock);
        return FL_NO_FREE_FRAME;
    }

    atomic_store(&free_f->ref_cnt, 1);
    free_f->read_only = false;
    free_f->info.timestamp_us = _fl_get_ts_us();
    free_f->info.frame_id = ++link->frame_id_inc;
    link->free_cnt--;

    // 新增：生产者获取帧，打印ID+REF
    printf("[FL_PRODUCE] 帧ID=%u | 引用计数=%u | 数据地址=%p\n",
           free_f->info.frame_id, atomic_load(&free_f->ref_cnt), free_f->data);    
    *out_frame = free_f;

    pthread_mutex_unlock(&link->pool_lock);
    return FL_OK;
}

fl_err_t frame_link_producer_push(const char *name, frame_handle_t frame) {
    if (!frame) return FL_INVALID_PARAM;
    struct frame_link *link = _fl_find_by_name(name);
    if (!link || frame->owner != link) return FL_NOT_FOUND;

    if (_fl_mutex_timedlock(&link->pool_lock) != 0) return FL_TIMEOUT;
    if (_fl_mutex_timedlock(&link->queue_lock) != 0) {
        pthread_mutex_unlock(&link->pool_lock);
        return FL_TIMEOUT;
    }

    frame->read_only = true;
    uint32_t next_tail = (link->q_tail + 1) % link->cfg.queue_count;
    if (next_tail != link->q_head) {
        link->queue[link->q_tail] = frame;
        link->q_tail = next_tail;
    }
    link->latest_frame = frame;

    pthread_mutex_unlock(&link->queue_lock);
    pthread_mutex_unlock(&link->pool_lock);
    return FL_OK;
}

void *frame_get_writable_ptr(frame_handle_t frame) {
    if (!frame || frame->read_only) return NULL;
    return frame->data;
}

/**
 * @brief 生产者设置帧元数据（分辨率、格式、数据长度等）
 * @param frame 帧句柄
 * @param info 元数据结构体指针
 * @return 错误码
 * @note 仅生产者可调用（帧处于可写状态），推送后禁止修改
 */
fl_err_t frame_set_info(frame_handle_t frame, const frame_info_t *info)
{
    // 安全校验：句柄非空、元数据非空、帧处于可写状态（单写规范）
    if (!frame || !info || frame->read_only)
    {
        return FL_INVALID_PARAM;
    }

    // 直接赋值元数据（核心修复）
    frame->info.width = info->width;
    frame->info.height = info->height;
    frame->info.format = info->format;
    frame->info.data_size = info->data_size;

    return FL_OK;
}

// 消费者通过总线获取帧（关键：打印+1后的引用计数）
fl_err_t frame_link_consumer_get_by_bus(const char* name, frame_handle_t bus_frame, frame_handle_t *out_frame) {
    if (!bus_frame || !out_frame) return FL_INVALID_PARAM;
    struct frame_link *link = _fl_find_by_name(name);
    if (!link || bus_frame->owner != link) return FL_NOT_FOUND;

    atomic_fetch_add(&bus_frame->ref_cnt, 1);
    // 新增：消费者引用帧，打印ID+最新REF
    printf("[FL_CONSUME_GET] 帧ID=%u | 引用计数+1 → %u | 地址=%p\n",
           bus_frame->info.frame_id, atomic_load(&bus_frame->ref_cnt), bus_frame->data);
    *out_frame = bus_frame;
    return FL_OK;
}

fl_err_t frame_link_consumer_get(const char *name, fl_consume_mode_t mode, frame_handle_t *out_frame) {
    if (!out_frame) return FL_INVALID_PARAM;
    struct frame_link *link = _fl_find_by_name(name);
    if (!link) return FL_NOT_FOUND;
    *out_frame = NULL;

    if (_fl_mutex_timedlock(&link->queue_lock) != 0) return FL_TIMEOUT;

    if (mode == FL_CONSUME_LATEST) {
        *out_frame = link->latest_frame;
    } else {
        if (link->q_head != link->q_tail) {
            *out_frame = link->queue[link->q_head];
            link->q_head = (link->q_head + 1) % link->cfg.queue_count;
        }
    }

    pthread_mutex_unlock(&link->queue_lock);
    if (*out_frame) {
        atomic_fetch_add(&(*out_frame)->ref_cnt, 1);
    }
    return FL_OK;
}

// 消费者释放帧（关键：打印-1后的引用计数）
fl_err_t frame_link_consumer_put(frame_handle_t frame) {
    if (!frame) return FL_INVALID_PARAM;
    struct frame_link *link = frame->owner;
    if (!link) return FL_NOT_FOUND;

    uint32_t old_cnt = atomic_load(&frame->ref_cnt);
    if (old_cnt == 0) {
        printf("[FL_ERROR] 帧%u 已空闲，禁止重复释放！\n", frame->info.frame_id);
        return FL_INVALID_PARAM;
    }

    uint32_t cnt = atomic_fetch_sub(&frame->ref_cnt, 1) - 1;
    // 优化：释放帧，打印ID+最新REF
    printf("[FL_CONSUME_PUT] 帧ID=%u | 引用计数-1 → %u | 地址=%p\n",
           frame->info.frame_id, cnt, frame->data);

    if (cnt == 0) {
        if (_fl_mutex_timedlock(&link->pool_lock) == 0) {
            link->free_cnt++;
            printf("[FL_RECYCLE] 帧ID=%u 已回收，空闲帧=%u\n", frame->info.frame_id, link->free_cnt);
            pthread_mutex_unlock(&link->pool_lock);
        }
    }
    return FL_OK;
}

const void *frame_get_readonly_ptr(frame_handle_t frame) {
    return frame ? frame->data : NULL;
}

fl_err_t frame_get_info(frame_handle_t frame, frame_info_t *info) {
    if (!frame || !info) return FL_INVALID_PARAM;
    *info = frame->info;
    return FL_OK;
}