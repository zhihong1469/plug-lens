#define _GNU_SOURCE
#include "thread.h"
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

// ==========================================================================
// Internal Macros
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
// Internal Function: Map generic priority to system real-time priority
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
// Internal Wrapper: Thread entry with running state management
// ==========================================================================
static void* _thread_entry_wrapper(void *arg)
{
    thread_t *thread = (thread_t*)arg;
    if (!thread) return NULL;

    thread->running = true;
    THREAD_LOG_I("Thread [%s] started", thread->attr.name ?: "unnamed");

    void *retval = NULL;
    if (thread->entry) retval = thread->entry(thread->user_data);

    thread->running = false;
    THREAD_LOG_I("Thread [%s] exited", thread->attr.name ?: "unnamed");
    return retval;
}

// ==========================================================================
// Public API Implementation
// ==========================================================================
void thread_attr_init(thread_attr_t *attr)
{
    if (!attr) return;
    memset(attr, 0, sizeof(thread_attr_t));
    attr->name = NULL;
    attr->stack_size = 0;
    attr->priority = THREAD_PRIORITY_NORMAL;
    attr->detached = false;
    attr->joinable = true;
}

thread_err_t thread_create(thread_t *thread,
                           const thread_attr_t *attr,
                           void *(*entry)(void *), void *user_data)
{
    if (!thread || !entry) return THREAD_ERR_NULL_PARAM;
    if (thread->initialized && thread->running) return THREAD_ERR_ALREADY_RUNNING;

    memset(thread, 0, sizeof(thread_t));
    thread->entry = entry;
    thread->user_data = user_data;
    thread->initialized = true;

    if (attr) memcpy(&thread->attr, attr, sizeof(thread_attr_t));
    else thread_attr_init(&thread->attr);

    pthread_attr_t pthread_attr;
    pthread_attr_init(&pthread_attr);

    if (thread->attr.stack_size > 0)
        pthread_attr_setstacksize(&pthread_attr, thread->attr.stack_size);

    if (thread->attr.detached) {
        pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);
        thread->attr.joinable = false;
    } else {
        pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_JOINABLE);
        thread->attr.joinable = true;
    }

    int ret = pthread_create(&thread->thread_id, &pthread_attr, _thread_entry_wrapper, thread);
    pthread_attr_destroy(&pthread_attr);

    if (ret != 0) {
        THREAD_LOG_E("pthread create failed: %s", strerror(ret));
        thread->initialized = false;
        return THREAD_ERR_CREATE_FAILED;
    }

#ifdef __linux__
    if (thread->attr.name) thread_set_name(thread, thread->attr.name);
#endif

    THREAD_LOG_I("Thread created: [%s]", thread->attr.name ?: "unnamed");
    return THREAD_OK;
}


thread_err_t thread_create_rt(thread_t *thread,
                              const char *name,
                              size_t stack_size,
                              void *(*entry)(void *),
                              void *user_data,
                              int rt_prio,
                              uint32_t cpu_id)
{
    thread_err_t ret;
    thread_attr_t attr;

    // Initialize attributes
    thread_attr_init(&attr);
    attr.name = name;
    attr.stack_size = stack_size;
    attr.priority = THREAD_PRIORITY_HIGH;

    // Create base thread
    ret = thread_create(thread, &attr, entry, user_data);
    if (ret != THREAD_OK) return ret;

    // Set real-time priority (FIFO)
    thread_set_rt_priority(thread, THREAD_SCHED_FIFO, rt_prio);

    // Bind CPU core
    thread_set_affinity(thread, cpu_id);

    return THREAD_OK;
}

thread_err_t thread_join(thread_t *thread, void **retval)
{
    if (!thread) return THREAD_ERR_NULL_PARAM;
    if (!thread->initialized) return THREAD_ERR_NOT_INITIALIZED;
    if (!thread->attr.joinable) return THREAD_ERR_INVALID_ATTR;

    int ret = pthread_join(thread->thread_id, retval);
    if (ret != 0) {
        THREAD_LOG_E("join failed: %s", strerror(ret));
        return THREAD_ERR_JOIN_FAILED;
    }
    thread->running = false;
    return THREAD_OK;
}

thread_err_t thread_detach(thread_t *thread)
{
    if (!thread || !thread->initialized) return THREAD_ERR_NULL_PARAM;
    if (thread->attr.detached) return THREAD_OK;

    int ret = pthread_detach(thread->thread_id);
    if (ret != 0) return THREAD_ERR_DETACH_FAILED;

    thread->attr.detached = true;
    thread->attr.joinable = false;
    return THREAD_OK;
}

bool thread_is_running(thread_t *thread)
{
    return thread && thread->initialized && thread->running;
}

const char* thread_get_name(thread_t *thread)
{
    return (thread && thread->initialized) ? thread->attr.name : NULL;
}

pthread_t thread_self_id(void) { return pthread_self(); }

void thread_sleep_ms(uint32_t ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR);
}

void thread_sleep_us(uint64_t us)
{
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR);
}

void thread_yield(void) { sched_yield(); }
void thread_stop(thread_t *thread) { if (thread) thread->running = false; }

// ==========================================================================
// Linux Real-time Thread Implementation
// ==========================================================================
#ifdef __linux__
thread_err_t thread_set_rt_priority(thread_t *thread, thread_sched_policy_t policy, int prio)
{
    if (!thread || !thread->initialized || prio < 1 || prio > 99)
        return THREAD_ERR_INVALID_ATTR;

    int sched_policy = SCHED_OTHER;
    if (policy == THREAD_SCHED_FIFO) sched_policy = SCHED_FIFO;
    else if (policy == THREAD_SCHED_RR) sched_policy = SCHED_RR;

    struct sched_param param = {.sched_priority = prio};
    int ret = pthread_setschedparam(thread->thread_id, sched_policy, &param);

    if (ret != 0) {
        THREAD_LOG_E("set RT prio failed: %s (need root)", strerror(ret));
        return THREAD_ERR_INVALID_ATTR;
    }

    THREAD_LOG_I("set RT success: policy=%d, prio=%d", policy, prio);
    return THREAD_OK;
}

thread_err_t thread_set_affinity(thread_t *thread, uint32_t cpu_id)
{
    if (!thread || !thread->initialized) return THREAD_ERR_NULL_PARAM;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);

    int ret = pthread_setaffinity_np(thread->thread_id, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        THREAD_LOG_E("set affinity failed: %s", strerror(ret));
        return THREAD_ERR_INVALID_ATTR;
    }
    return THREAD_OK;
}

void thread_set_name(thread_t *thread, const char *name)
{
    if (thread && name) prctl(PR_SET_NAME, name, 0, 0, 0);
}
#endif