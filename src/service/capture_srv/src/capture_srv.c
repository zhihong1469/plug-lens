// src/service/capture_srv/src/capture_srv.c
#include "capture_srv.h"
#include "frame_link.h"
#include "module_fsm.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// ==========================================================================
// 【核心】采集服务专属：状态迁移规则表（定义在业务层）
// ==========================================================================
static module_state_trans_t g_capture_srv_trans_table[] = {
    // 初始化流程
    {MODULE_STATE_IDLE,           MODULE_EVENT_INIT,      MODULE_STATE_INITIALIZING},
    {MODULE_STATE_INITIALIZING,   MODULE_EVENT_INIT_OK,   MODULE_STATE_READY},
    {MODULE_STATE_INITIALIZING,   MODULE_EVENT_INIT_FAIL, MODULE_STATE_ERROR},
    
    // 启动流程
    {MODULE_STATE_READY,          MODULE_EVENT_START,     MODULE_STATE_STARTING},
    {MODULE_STATE_STARTING,       MODULE_EVENT_START_OK,  MODULE_STATE_RUNNING},
    {MODULE_STATE_STARTING,       MODULE_EVENT_START_FAIL,MODULE_STATE_ERROR},
    
    // 停止流程
    {MODULE_STATE_RUNNING,        MODULE_EVENT_STOP,      MODULE_STATE_STOPPING},
    {MODULE_STATE_STOPPING,       MODULE_EVENT_STOP_OK,   MODULE_STATE_IDLE},
    
    // 异常流程
    {MODULE_STATE_RUNNING,        MODULE_EVENT_ERROR,     MODULE_STATE_ERROR},
    {MODULE_STATE_ERROR,          MODULE_EVENT_ERROR_CLEAR, MODULE_STATE_IDLE},
    
    // 销毁流程
    {MODULE_STATE_IDLE,           MODULE_EVENT_DEINIT,    MODULE_STATE_DEINITIALIZING},
    {MODULE_STATE_READY,          MODULE_EVENT_DEINIT,    MODULE_STATE_DEINITIALIZING},
    {MODULE_STATE_ERROR,          MODULE_EVENT_DEINIT,    MODULE_STATE_DEINITIALIZING},
    {MODULE_STATE_DEINITIALIZING, MODULE_EVENT_DEINIT_OK, MODULE_STATE_DEINIT},
};
static const uint32_t g_capture_srv_trans_len = sizeof(g_capture_srv_trans_table) / sizeof(g_capture_srv_trans_table[0]);

// ==========================================================================
// 内部上下文
// ==========================================================================
typedef struct {
    frame_link_handle_t link_handle;
    capture_srv_config_t config;
    module_fsm_handle_t fsm_handle; // 持有通用基类的句柄
    pthread_mutex_t lock;
} capture_srv_context_t;

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static int _capture_srv_fsm_event_handler(module_event_t event, void *user_data);

// ==========================================================================
// 对外API实现
// ==========================================================================

int capture_srv_init(const capture_srv_config_t *config,
                     module_state_change_cb_t fsm_state_cb,
                     void *fsm_user_data,
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
    pthread_mutex_init(&ctx->lock, NULL);

    // 1. 先初始化 Link层
    video_err_t err = frame_link_init(&ctx->config.link_config, &ctx->link_handle);
    if (err != VIDEO_OK) {
        LOG_E("Capture Srv: Failed to init link layer");
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }

    // 2. 【核心】创建通用 module_fsm，传入我们的业务配置
    module_fsm_config_t fsm_cfg = {0};
    fsm_cfg.module_name = "capture_srv";
    fsm_cfg.trans_table = g_capture_srv_trans_table;
    fsm_cfg.trans_table_len = g_capture_srv_trans_len;
    fsm_cfg.event_handler = _capture_srv_fsm_event_handler; // 我们的业务处理函数
    fsm_cfg.state_cb = fsm_state_cb; // 上层（Global FSM）的回调
    fsm_cfg.user_data = fsm_user_data;

    int ret = module_fsm_create(&fsm_cfg, &ctx->fsm_handle);
    if (ret != 0) {
        LOG_E("Capture Srv: Failed to create module FSM");
        frame_link_deinit(ctx->link_handle);
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }

    // 3. 给状态机投 INIT 事件，启动初始化流程
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT);
    // 初始化 Link层 是同步的，这里直接投 INIT_OK
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT_OK);

    // 自动启动
    if (ctx->config.auto_start) {
        LOG_I("Capture Srv: Auto start enabled");
        module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START);
    }

    *out_handle = (capture_srv_handle_t)ctx;
    LOG_I("Capture Srv: Initialized successfully");
    return 0;
}

module_fsm_handle_t capture_srv_get_fsm(capture_srv_handle_t handle)
{
    if (handle == NULL) return NULL;
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;
    return ctx->fsm_handle;
}

int capture_srv_get_frame(capture_srv_handle_t handle,
                          video_frame_t *frame,
                          uint32_t timeout_ms)
{
    if (handle == NULL || frame == NULL) return -1;
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;

    // 只有在 RUNNING 状态才允许取帧
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
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;
    return frame_link_put_frame(ctx->link_handle, frame);
}

int capture_srv_deinit(capture_srv_handle_t handle)
{
    if (handle == NULL) return -1;
    capture_srv_context_t *ctx = (capture_srv_context_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    // 投 DEINIT 事件
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT);
    
    // 销毁 Link层
    frame_link_deinit(ctx->link_handle);
    
    // 投 DEINIT_OK
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT_OK);
    
    // 销毁状态机
    module_fsm_destroy(ctx->fsm_handle);

    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    
    LOG_I("Capture Srv: Deinitialized");
    return 0;
}

// ==========================================================================
// 【核心】业务层事件处理函数（在这里执行真正的硬件操作）
// ==========================================================================
static int _capture_srv_fsm_event_handler(module_event_t event, void *user_data)
{
    // 注意：user_data 这里是 fsm_cfg.user_data（即 Global FSM 的数据）
    // 如果需要访问 capture_srv_context_t，需要用其他方式（比如把 ctx 也包进去）
    // 为了简化演示，这里我们假设 event_handler 只做简单的决策
    // 实际项目中，可以在 capture_srv_context_t 里再包一层
    
    LOG_I("Capture Srv: Business event handler for event %s", module_event_to_str(event));
    
    // 这里只是演示，实际的 start/stop link 操作可以放在这里
    // 或者放在 capture_srv_init/capture_srv_deinit 里同步执行
    
    return 0; // 允许迁移
}