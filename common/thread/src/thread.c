#include "thread.h"
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

// ==========================================================================
// 内部辅助宏
// ==========================================================================
#if THREAD_ENABLE_LOG
    #define THREAD_LOG_I(fmt, ...) LOG_I(fmt, ##__VA_ARGS__)
    #define THREAD_LOG_E(fmt, ...) LOG_E(fmt, ##__VA_ARGS__)
    #define THREAD_LOG_W(fmt, ...) LOG_W(fmt, ##__VA_ARGS__)
#else
    #define THREAD_LOG_I(fmt, ...) do { } while(0)
    #define THREAD_LOG_E(fmt, ...) do { } while(0)
    #define THREAD_LOG_W(fmt, ...) do { } while(0)
#endif

// ==========================================================================
// 内部辅助函数：优先级映射
// ==========================================================================
static int _thread_priority_to_system(thread_priority_t priority)
{
    int min_prio = sched_get_priority_min(SCHED_FIFO);
    int max_prio = sched_get_priority_max(SCHED_FIFO);
    int range = max_prio - min_prio;

    switch (priority) {
        case THREAD_PRIORITY_LOWEST:  return min_prio;
        case THREAD_PRIORITY_LOW:     return min_prio + (range / 4);
        case THREAD_PRIORITY_NORMAL:  return min_prio + (range / 2);
        case THREAD_PRIORITY_HIGH:    return min_prio + (range * 3 / 4);
        case THREAD_PRIORITY_HIGHEST: return max_prio;
        default:                       return min_prio + (range / 2);
    }
}

// ==========================================================================
// 内部线程入口包装函数
// ==========================================================================
static void* _thread_entry_wrapper(void *arg)
{
    thread_t *thread = (thread_t*)arg;
    if (thread == NULL) {
        return NULL;
    }

    thread->running = true;
    
    THREAD_LOG_I("Thread [%s] started", thread->attr.name ? thread->attr.name : "unnamed");

    // 调用用户入口函数
    void *retval = NULL;
    if (thread->entry != NULL) {
        retval = thread->entry(thread->user_data);
    }

    thread->running = false;
    
    THREAD_LOG_I("Thread [%s] exited", thread->attr.name ? thread->attr.name : "unnamed");

    return retval;
}

// ==========================================================================
// 对外 API 实现
// ==========================================================================

void thread_attr_init(thread_attr_t *attr)
{
    if (attr == NULL) {
        return;
    }

    memset(attr, 0, sizeof(thread_attr_t));
    attr->name = NULL;
    attr->stack_size = 0;          // 使用系统默认
    attr->priority = THREAD_PRIORITY_NORMAL;
    attr->detached = false;
    attr->joinable = true;
}

thread_err_t thread_create(thread_t *thread,
                           const thread_attr_t *attr,
                           void *(*entry)(void *),
                           void *user_data)
{
    if (thread == NULL || entry == NULL) {
        return THREAD_ERR_NULL_PARAM;
    }

    if (thread->initialized && thread->running) {
        THREAD_LOG_W("Thread already running");
        return THREAD_ERR_ALREADY_RUNNING;
    }

    // 初始化线程结构体
    memset(thread, 0, sizeof(thread_t));
    thread->entry = entry;
    thread->user_data = user_data;
    thread->running = false;
    thread->initialized = true;

    // 拷贝属性
    if (attr != NULL) {
        memcpy(&thread->attr, attr, sizeof(thread_attr_t));
    } else {
        thread_attr_init(&thread->attr);
    }

    // 初始化 pthread 属性
    pthread_attr_t pthread_attr;
    pthread_attr_init(&pthread_attr);

    // 设置栈大小
    if (thread->attr.stack_size > 0) {
        pthread_attr_setstacksize(&pthread_attr, thread->attr.stack_size);
    }

    // 设置分离状态
    if (thread->attr.detached) {
        pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);
        thread->attr.joinable = false;
    } else {
        pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_JOINABLE);
        thread->attr.joinable = true;
    }

    // 设置调度策略和优先级（仅在非分离状态下尝试）
    if (!thread->attr.detached) {
        struct sched_param param;
        memset(&param, 0, sizeof(param));
        param.sched_priority = _thread_priority_to_system(thread->attr.priority);
        
        // 尝试设置调度策略（可能失败，忽略错误）
        pthread_attr_setschedpolicy(&pthread_attr, SCHED_FIFO);
        pthread_attr_setschedparam(&pthread_attr, &param);
        pthread_attr_setinheritsched(&pthread_attr, PTHREAD_EXPLICIT_SCHED);
    }

    // 创建线程
    int ret = pthread_create(&thread->thread_id,
                             &pthread_attr,
                             _thread_entry_wrapper,
                             thread);

    // 销毁 pthread 属性
    pthread_attr_destroy(&pthread_attr);

    if (ret != 0) {
        THREAD_LOG_E("Failed to create thread, errno=%d (%s)", ret, strerror(ret));
        thread->initialized = false;
        return THREAD_ERR_CREATE_FAILED;
    }

    THREAD_LOG_I("Thread created: [%s]", thread->attr.name ? thread->attr.name : "unnamed");

    return THREAD_OK;
}

thread_err_t thread_join(thread_t *thread, void **retval)
{
    if (thread == NULL) {
        return THREAD_ERR_NULL_PARAM;
    }

    if (!thread->initialized) {
        return THREAD_ERR_NOT_INITIALIZED;
    }

    if (!thread->attr.joinable) {
        THREAD_LOG_W("Thread is not joinable (detached)");
        return THREAD_ERR_INVALID_ATTR;
    }

    int ret = pthread_join(thread->thread_id, retval);
    if (ret != 0) {
        THREAD_LOG_E("Failed to join thread, errno=%d (%s)", ret, strerror(ret));
        return THREAD_ERR_JOIN_FAILED;
    }

    thread->running = false;
    return THREAD_OK;
}

thread_err_t thread_detach(thread_t *thread)
{
    if (thread == NULL) {
        return THREAD_ERR_NULL_PARAM;
    }

    if (!thread->initialized) {
        return THREAD_ERR_NOT_INITIALIZED;
    }

    if (thread->attr.detached) {
        return THREAD_OK; // 已经是分离状态
    }

    int ret = pthread_detach(thread->thread_id);
    if (ret != 0) {
        THREAD_LOG_E("Failed to detach thread, errno=%d (%s)", ret, strerror(ret));
        return THREAD_ERR_DETACH_FAILED;
    }

    thread->attr.detached = true;
    thread->attr.joinable = false;
    
    THREAD_LOG_I("Thread [%s] detached", thread->attr.name ? thread->attr.name : "unnamed");

    return THREAD_OK;
}

bool thread_is_running(thread_t *thread)
{
    if (thread == NULL || !thread->initialized) {
        return false;
    }
    return thread->running;
}

const char* thread_get_name(thread_t *thread)
{
    if (thread == NULL || !thread->initialized) {
        return NULL;
    }
    return thread->attr.name;
}

pthread_t thread_self_id(void)
{
    return pthread_self();
}

void thread_sleep_ms(uint32_t ms)
{
    if (ms == 0) {
        return;
    }

    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        // 被信号中断，继续休眠
    }
}

void thread_sleep_us(uint64_t us)
{
    if (us == 0) {
        return;
    }

    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        // 被信号中断，继续休眠
    }
}

void thread_yield(void)
{
    sched_yield();
}
