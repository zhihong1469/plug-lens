#include "capture_srv.h"
#include "frame_link.h"
#include "module_fsm.h"
#include "event_bus.h"
#include "data_bus.h"
#include "thread.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "main.h"

static module_state_trans_t g_capture_srv_trans_table[] = {
    {MODULE_STATE_IDLE,           MODULE_EVENT_INIT,      MODULE_STATE_INITIALIZING},
    {MODULE_STATE_INITIALIZING,   MODULE_EVENT_INIT_OK,   MODULE_STATE_READY},
    {MODULE_STATE_INITIALIZING,   MODULE_EVENT_INIT_FAIL, MODULE_STATE_ERROR},
    {MODULE_STATE_READY,          MODULE_EVENT_START,     MODULE_STATE_STARTING},
    {MODULE_STATE_STARTING,       MODULE_EVENT_START_OK,  MODULE_STATE_RUNNING},
    {MODULE_STATE_STARTING,       MODULE_EVENT_START_FAIL,MODULE_STATE_ERROR},
    {MODULE_STATE_RUNNING,        MODULE_EVENT_STOP,      MODULE_STATE_STOPPING},
    {MODULE_STATE_STOPPING,       MODULE_EVENT_STOP_OK,   MODULE_STATE_READY},
    {MODULE_STATE_RUNNING,        MODULE_EVENT_ERROR,     MODULE_STATE_ERROR},
    {MODULE_STATE_ERROR,          MODULE_EVENT_ERROR_CLEAR, MODULE_STATE_IDLE},
    {MODULE_STATE_IDLE,           MODULE_EVENT_DEINIT,    MODULE_STATE_DEINITIALIZING},
    {MODULE_STATE_READY,          MODULE_EVENT_DEINIT,    MODULE_STATE_DEINITIALIZING},
    {MODULE_STATE_ERROR,          MODULE_EVENT_DEINIT,    MODULE_STATE_DEINITIALIZING},
    {MODULE_STATE_DEINITIALIZING, MODULE_EVENT_DEINIT_OK, MODULE_STATE_DEINIT},
};
static const uint32_t g_capture_srv_trans_len = sizeof(g_capture_srv_trans_table) / sizeof(g_capture_srv_trans_table[0]);

typedef struct {
    capture_srv_config_t config;
    capture_srv_callbacks_t callbacks;
    
    frame_link_handle_t link_handle;
    module_fsm_handle_t fsm_handle;
    
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;
    
    thread_t capture_thread;
    bool thread_running;
    
    bool is_created;
    pthread_mutex_t lock;
} capture_srv_ctx_t;

static void* _capture_srv_async_thread(void *arg);
static int _capture_srv_fsm_event_handler(module_event_t event, void *user_data);
static void _capture_srv_fsm_state_relay(const char *module_name,
                                          module_state_t old_state,
                                          module_state_t new_state,
                                          void *user_data);
static int _capture_srv_process_and_publish_frame(capture_srv_ctx_t *ctx, const video_frame_t *frame);

int capture_srv_create(const capture_srv_config_t *config,
                       capture_srv_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)malloc(sizeof(capture_srv_ctx_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(capture_srv_ctx_t));

    memcpy(&ctx->config, config, sizeof(capture_srv_config_t));
    memcpy(&ctx->callbacks, &config->callbacks, sizeof(capture_srv_callbacks_t));
    ctx->evt_bus = config->evt_bus;
    ctx->data_bus = config->data_bus;
    pthread_mutex_init(&ctx->lock, NULL);
    ctx->thread_running = false;

    LOG_I("Capture Srv: Initializing Link layer...");
    video_err_t err = frame_link_init(&ctx->config.link_cfg, g_app_ctx.exit_pipe[0], &ctx->link_handle);
    if (err != VIDEO_OK) {
        LOG_E("Capture Srv: Failed to init link layer (err=%d)", err);
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }

    LOG_I("Capture Srv: Creating Module FSM...");
    module_fsm_config_t fsm_cfg = {0};
    fsm_cfg.module_name = "capture_srv";
    fsm_cfg.trans_table = g_capture_srv_trans_table;
    fsm_cfg.trans_table_len = g_capture_srv_trans_len;
    fsm_cfg.event_handler = _capture_srv_fsm_event_handler;
    fsm_cfg.state_cb = _capture_srv_fsm_state_relay;
    fsm_cfg.user_data = ctx;

    int ret = module_fsm_create(&fsm_cfg, &ctx->fsm_handle);
    if (ret != 0) {
        LOG_E("Capture Srv: Failed to create Module FSM");
        frame_link_deinit(ctx->link_handle);
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }

    ctx->is_created = true;
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT);
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT_OK);

    if (ctx->config.auto_start) {
        LOG_I("Capture Srv: Auto start enabled");
        module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START);
    }

    *out_handle = (capture_srv_handle_t)ctx;
    LOG_I("Capture Srv: Created successfully");
    return 0;
}

module_fsm_handle_t capture_srv_get_fsm(capture_srv_handle_t handle)
{
    if (handle == NULL) return NULL;
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)handle;
    return ctx->fsm_handle;
}

int capture_srv_get_frame(capture_srv_handle_t handle,
                          video_frame_t *frame,
                          uint32_t timeout_ms)
{
    if (handle == NULL || frame == NULL) return -1;
    // 【修复编译错误】修正指针强制类型转换
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)handle;

    module_state_t state = module_fsm_get_state(ctx->fsm_handle);
    if (state != MODULE_STATE_RUNNING) {
        LOG_W("Capture Srv: Get frame called in state %s", module_state_to_str(state));
        return -1;
    }

    return frame_link_get_frame(ctx->link_handle, frame, timeout_ms);
}

int capture_srv_put_frame(capture_srv_handle_t handle,
                          const video_frame_t *frame)
{
    if (handle == NULL || frame == NULL) return -1;
    // 【修复编译错误】修正指针强制类型转换
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)handle;
    return frame_link_put_frame(ctx->link_handle, frame);
}

int capture_srv_destroy(capture_srv_handle_t handle)
{
    if (handle == NULL) return -1;
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    if (!ctx->is_created) {
        pthread_mutex_unlock(&ctx->lock);
        return 0;
    }

    if (ctx->thread_running) {
        ctx->thread_running = false;
        thread_join(&ctx->capture_thread, NULL);
    }

    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT);
    frame_link_stop_stream(ctx->link_handle);
    frame_link_deinit(ctx->link_handle);
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT_OK);
    module_fsm_destroy(ctx->fsm_handle);

    ctx->is_created = false;
    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    
    LOG_I("Capture Srv: Destroyed");
    return 0;
}

static void* _capture_srv_async_thread(void *arg)
{
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)arg;
    int ret = 0;

    while (g_app_ctx.app_running && ctx->thread_running)
    {

        if (!g_app_ctx.app_running || !ctx->thread_running) {
            break;
        }

        module_state_t state = module_fsm_get_state(ctx->fsm_handle);
        if (state != MODULE_STATE_RUNNING) {
            thread_sleep_ms(10);
            continue;
        }

        video_frame_t frame;
        ret = frame_link_get_frame(ctx->link_handle, &frame, 50);
        if (ret)
        {
            LOG_D("Capture Srv: Failed frame_link_get_frame ret=%d", ret);
        }
        else 
        {
            LOG_D("Capture Srv: Got frame, ts=%llu, len=%u", 
                  frame.timestamp, frame.length);
        }
        if (ret != VIDEO_OK) {
            if (!g_app_ctx.app_running || !ctx->thread_running) {
                break;
            }
            continue;
        }
        ret = _capture_srv_process_and_publish_frame(ctx, &frame);
        // ============== 【新增调试日志5】发布完成 ==============
        if (ret)
        {
            LOG_D("Capture Srv: Frame published Failed:%d", ret);
        }
        
        frame_link_put_frame(ctx->link_handle, &frame);
    }

    LOG_I("Capture Srv: Async capture thread exited");
    return NULL;
}

static int _capture_srv_fsm_event_handler(module_event_t event, void *user_data)
{
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)user_data;
    if (ctx == NULL) return -1;

    LOG_I("Capture Srv: FSM Event Handler received: %s", module_event_to_str(event));
    return 0;
}

static void _capture_srv_fsm_state_relay(const char *module_name,
                                          module_state_t old_state,
                                          module_state_t new_state,
                                          void *user_data)
{
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)user_data;
    if (ctx == NULL) return;

    LOG_I("Capture Srv: State changed: %s -> %s", 
          module_state_to_str(old_state), 
          module_state_to_str(new_state));

    if (new_state == MODULE_STATE_STARTING) {
        LOG_I("Capture Srv: Starting HAL stream...");
        video_err_t err = frame_link_start_stream(ctx->link_handle);
        if (err != VIDEO_OK) {
            LOG_E("Capture Srv: Failed to start HAL stream");
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_FAIL);
            return;
        }

        thread_attr_t attr;
        thread_attr_init(&attr);
        attr.name = "capture_thread";
        attr.priority = THREAD_PRIORITY_HIGH;
        attr.stack_size = 128 * 1024;
        attr.joinable = true;
        attr.detached = false;

        ctx->thread_running = true;
        thread_err_t terr = thread_create(&ctx->capture_thread, &attr, _capture_srv_async_thread, ctx);
        if (terr == THREAD_OK) {
            LOG_I("Capture Srv: Capture thread created");
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_OK);
        } else {
            LOG_E("Capture Srv: Failed to create capture thread");
            frame_link_stop_stream(ctx->link_handle);
            ctx->thread_running = false;
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_FAIL);
        }
    }
    else if (new_state == MODULE_STATE_STOPPING) {
        LOG_I("Capture Srv: Stopping capture thread...");
        
        if (ctx->thread_running) {
            ctx->thread_running = false;
            frame_link_stop_stream(ctx->link_handle);
            thread_join(&ctx->capture_thread, NULL);
        } else {
            frame_link_stop_stream(ctx->link_handle);
        }
        module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_STOP_OK);
    }

    if (ctx->callbacks.state_change_cb != NULL) {
        ctx->callbacks.state_change_cb(module_name, old_state, new_state, ctx->callbacks.user_data);
    }
}

static int _capture_srv_process_and_publish_frame(capture_srv_ctx_t *ctx, const video_frame_t *frame)
{
    if (ctx == NULL || frame == NULL) return -1;
    int ret = 0;
    data_bus_item_handle_t item = NULL;
    size_t data_size = frame->length;
    
    ret = data_bus_alloc(ctx->data_bus, DATA_TYPE_VIDEO_FRAME, data_size, "capture_srv", &item);
    if (ret != 0 || item == NULL) {
        LOG_E("Capture Srv: Failed to alloc data bus item, ret=%d", ret);
        return -1;
    }

    void *w_ptr = data_bus_get_writable_ptr(item);
    if (w_ptr == NULL) {
        LOG_E("Capture Srv: Failed to get writable ptr");
        data_bus_release(item);
        return -1;
    }
    memcpy(w_ptr, frame->data, data_size);

    ret = data_bus_publish(ctx->data_bus, item);
    if (ret != 0) {
        LOG_E("Capture Srv: Failed to publish data bus, ret=%d", ret);
        data_bus_release(item);
        return -1;
    }
    
    data_bus_release(item); 
    event_bus_publish_simple(ctx->evt_bus, EVENT_TYPE_CAP_FRAME_READY, "capture_srv");

    return 0;
}