/* SPDX-License-Identifier: MIT */
#include "frame_link.h"
#include "mem_adapter.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

/* ========================== 私有数据类型 ========================== */
struct frame {
    frame_info_t          info;
    uint8_t               *data;
    atomic_uint           ref_cnt;
    bool                  read_only;
    frame_link_handle_t   owner;
};

struct frame_link {
    frame_link_cfg_t      cfg;
    bool                  inited;

    uint8_t               *mem_pool;
    uint32_t              frame_total_size;  // 🔥 新增：单帧总大小(结构体+数据区)，编译期对齐
    uint32_t              free_cnt;

    struct frame          **queue;
    uint32_t              q_head;
    uint32_t              q_tail;
    struct frame          *latest_frame;

    pthread_mutex_t       pool_lock;
    pthread_mutex_t       queue_lock;
    uint32_t              frame_id_inc;
};

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

    struct frame_link *link = mem_calloc(1, sizeof(struct frame_link));
    if (!link) {
        pthread_mutex_unlock(&g_table_lock);
        return FL_NO_MEM;
    }

    // 🔥 核心修复1：计算单帧总大小(自动包含结构体对齐填充)
    const uint32_t frame_total_size = sizeof(struct frame) + cfg->max_frame_size;
    link->frame_total_size = frame_total_size;

    // 内存池总大小 = 帧数 × 单帧总大小
    link->mem_pool = mem_alloc(cfg->pool_count * frame_total_size);
    // 队列分配
    link->queue = mem_alloc(sizeof(struct frame *) * cfg->queue_count);
    if (!link->mem_pool || !link->queue) {
        mem_free(link->queue);
        mem_free(link->mem_pool);
        mem_free(link);
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

    // 🔥 核心修复2：独立块模式初始化每个帧
    // 内存布局：[帧0结构体][帧0数据区][帧1结构体][帧1数据区]...[帧N结构体][帧N数据区]
    for (uint32_t i = 0; i < cfg->pool_count; i++) {
        // 第i个帧的结构体地址 = 内存池基址 + i × 单帧总大小
        struct frame *f = (struct frame *)(link->mem_pool + i * frame_total_size);
        // 数据区地址 = 自身结构体地址 + 结构体大小(自动对齐)
        f->data = (uint8_t *)f + sizeof(struct frame);
        
        atomic_init(&f->ref_cnt, 0);
        f->read_only = false;
        f->owner = link;
        memset(f->data, 0, cfg->max_frame_size);

        // 调试打印：验证内存布局(首次运行可打开，确认无重叠)
        // printf("[FL_INIT] 帧%d: 结构体=%p, 数据区=%p, 结束=%p\n",
        //        i, f, f->data, f->data + cfg->max_frame_size);
    }

    link->inited = true;
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
    
    mem_free(link->queue);
    mem_free(link->mem_pool);
    mem_free(link);

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

    // 🔥 修复：遍历所有独立帧块
    for (uint32_t i = 0; i < link->cfg.pool_count; i++) {
        struct frame *f = (struct frame *)(link->mem_pool + i * link->frame_total_size);
        atomic_store(&f->ref_cnt, 0);
        f->read_only = false;
        memset(&f->info, 0, sizeof(frame_info_t));
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
    // 🔥 修复：遍历所有独立帧块
    for (uint32_t i = 0; i < link->cfg.pool_count; i++) {
        struct frame *f = (struct frame *)(link->mem_pool + i * link->frame_total_size);
        printf("[FL_CHECK] 帧%d 真实引用: %u\n", i, atomic_load(&f->ref_cnt));
        if (atomic_load(&f->ref_cnt) == 0) {
            free_f = f;
            break;
        }
    }

    if (!free_f) {
        link->free_cnt = 0;
        pthread_mutex_unlock(&link->pool_lock);
        return FL_NO_FREE_FRAME;
    }

    atomic_store(&free_f->ref_cnt, 1);
    free_f->read_only = false;
    free_f->info.timestamp_us = _fl_get_ts_us();
    free_f->info.frame_id = ++link->frame_id_inc;
    link->free_cnt--;

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
    } else {
        printf("[FL_WARN] 帧队列满，丢弃旧帧\n");
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

// 保护帧ID/时间戳，不被上层覆盖
fl_err_t frame_set_info(frame_handle_t frame, const frame_info_t *info)
{
    if (!frame || !info || frame->read_only)
    {
        return FL_INVALID_PARAM;
    }

    frame->info.width = info->width;
    frame->info.height = info->height;
    frame->info.format = info->format;
    frame->info.data_size = info->data_size;
    // 禁止修改：frame_id / timestamp_us 由内核自动生成
    return FL_OK;
}

// 加锁，线程安全
fl_err_t frame_link_consumer_get_by_bus(const char* name, frame_handle_t bus_frame, frame_handle_t *out_frame) {
    if (!bus_frame || !out_frame) return FL_INVALID_PARAM;
    struct frame_link *link = _fl_find_by_name(name);
    if (!link || bus_frame->owner != link) return FL_NOT_FOUND;

    if (_fl_mutex_timedlock(&link->pool_lock) != 0) return FL_TIMEOUT;
    
    atomic_fetch_add(&bus_frame->ref_cnt, 1);
    printf("[FL_CONSUME_GET] 帧ID=%u | 引用计数+1 → %u | 地址=%p\n",
           bus_frame->info.frame_id, atomic_load(&bus_frame->ref_cnt), bus_frame->data);
    *out_frame = bus_frame;

    pthread_mutex_unlock(&link->pool_lock);
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

// 唯一释放接口
fl_err_t frame_link_put(frame_handle_t frame) {
    if (!frame) return FL_INVALID_PARAM;
    struct frame_link *link = frame->owner;
    if (!link) return FL_NOT_FOUND;

    uint32_t old_cnt = atomic_load(&frame->ref_cnt);
    if (old_cnt == 0) {
        printf("[FL_ERROR] 帧%u 已空闲，禁止重复释放！\n", frame->info.frame_id);
        return FL_INVALID_PARAM;
    }

    uint32_t cnt = atomic_fetch_sub(&frame->ref_cnt, 1) - 1;
    printf("[FL_CONSUME_PUT] 帧ID=%u | 引用计数-1 → %u | 地址=%p\n",
           frame->info.frame_id, cnt, frame->data);

    if (cnt == 0) {
        if (_fl_mutex_timedlock(&link->pool_lock) == 0) {
            link->free_cnt++;
            frame->read_only = false;
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