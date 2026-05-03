// src/service/capture_srv/src/capture_srv.c
#include "capture_srv.h"
#include "frame_link.h"
#include "module_fsm.h"
#include "event_bus.h"
#include "data_bus.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "main.h"
// ==========================================================================
// 【核心】采集服务专属：状态迁移规则表
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
    {MODULE_STATE_STOPPING,       MODULE_EVENT_STOP_OK,   MODULE_STATE_READY},
    
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
// 内部上下文结构体（完美封装）
// ==========================================================================
typedef struct {
    // 配置与回调
    capture_srv_config_t config;
    capture_srv_callbacks_t callbacks;
    
    // 下层依赖
    frame_link_handle_t link_handle;
    module_fsm_handle_t fsm_handle;
    
    // 总线句柄（缓存）
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;
    
    // 运行时状态
    bool is_created;
    pthread_mutex_t lock;
} capture_srv_ctx_t;

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static int _capture_srv_fsm_event_handler(module_event_t event, void *user_data);
static void _capture_srv_fsm_state_relay(const char *module_name,
                                          module_state_t old_state,
                                          module_state_t new_state,
                                          void *user_data);
static void _capture_srv_on_link_frame(const video_frame_t *frame, void *user_data);
static int _capture_srv_feed_bus(capture_srv_ctx_t *ctx, const video_frame_t *frame);

// ==========================================================================
// 对外API实现
// ==========================================================================

int capture_srv_create(const capture_srv_config_t *config,
                       capture_srv_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    // 1. 分配并清零上下文
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)malloc(sizeof(capture_srv_ctx_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(capture_srv_ctx_t));

    // 2. 拷贝配置与回调
    memcpy(&ctx->config, config, sizeof(capture_srv_config_t));
    memcpy(&ctx->callbacks, &config->callbacks, sizeof(capture_srv_callbacks_t));
    ctx->evt_bus = config->evt_bus;
    ctx->data_bus = config->data_bus;
    pthread_mutex_init(&ctx->lock, NULL);

    // 3. 初始化 Link层
    LOG_I("Capture Srv: Initializing Link layer...");
    video_err_t err = frame_link_init(&ctx->config.link_cfg, g_app_ctx.exit_pipe[0], &ctx->link_handle);
    if (err != VIDEO_OK) {
        LOG_E("Capture Srv: Failed to init link layer (err=%d)", err);
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }

    // 4. 注册 Link层 回调
    frame_link_register_frame_ready_cb(ctx->link_handle, _capture_srv_on_link_frame, ctx);

    // 5. 初始化 Module FSM
    LOG_I("Capture Srv: Creating Module FSM...");
    module_fsm_config_t fsm_cfg = {0};
    fsm_cfg.module_name = "capture_srv";
    fsm_cfg.trans_table = g_capture_srv_trans_table;
    fsm_cfg.trans_table_len = g_capture_srv_trans_len;
    fsm_cfg.event_handler = _capture_srv_fsm_event_handler;
    fsm_cfg.state_cb = _capture_srv_fsm_state_relay; // 【关键】先中继到自己
    fsm_cfg.user_data = ctx; // 【关键】user_data 传自己的 ctx

    int ret = module_fsm_create(&fsm_cfg, &ctx->fsm_handle);
    if (ret != 0) {
        LOG_E("Capture Srv: Failed to create Module FSM");
        frame_link_deinit(ctx->link_handle);
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }

    // 6. 启动初始化流程
    ctx->is_created = true;
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT);
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT_OK); // Link init 同步完成

    // 7. 自动启动（如果配置了）
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

    // 1. 投 DEINIT 事件
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT);

    // 2. 停止并销毁 Link层
    frame_link_stop(ctx->link_handle);
    frame_link_deinit(ctx->link_handle);

    // 3. 投 DEINIT_OK
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT_OK);

    // 4. 销毁 FSM
    module_fsm_destroy(ctx->fsm_handle);

    ctx->is_created = false;
    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    
    LOG_I("Capture Srv: Destroyed");
    return 0;
}

// ==========================================================================
// 【核心】内部辅助函数实现
// ==========================================================================

/**
 * @brief FSM 事件处理器（在锁内调用，只做决策，不做耗时操作）
 */
static int _capture_srv_fsm_event_handler(module_event_t event, void *user_data)
{
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)user_data;
    if (ctx == NULL) return -1;

    LOG_I("Capture Srv: FSM Event Handler received: %s", module_event_to_str(event));
    
    // 这里只做决策，耗时的硬件操作放在 _capture_srv_fsm_state_relay 里（锁外）
    return 0; // 允许所有迁移
}

/**
 * @brief 【关键】FSM 状态中继回调
 * 
 * 职责链：
 * Module FSM -> 此回调 -> 1. 执行硬件动作 (Start/Stop Link)
 *                        -> 2. 转发给上层回调 (Global FSM)
 */
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

    // 1. 执行硬件动作（根据新状态）
    if (new_state == MODULE_STATE_STARTING) {
        LOG_I("Capture Srv: Starting Link layer...");
        video_err_t err = frame_link_start(ctx->link_handle);
        if (err == VIDEO_OK) {
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_OK);
        } else {
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_FAIL);
        }
    }
    else if (new_state == MODULE_STATE_STOPPING) {
        LOG_I("Capture Srv: Stopping Link layer...");
        frame_link_stop(ctx->link_handle);
        module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_STOP_OK);
    }

    // 2. 【重要】转发给上层回调（Global FSM）
    if (ctx->callbacks.state_change_cb != NULL) {
        ctx->callbacks.state_change_cb(module_name, old_state, new_state, ctx->callbacks.user_data);
    }
}

/**
 * @brief Link层 帧就绪回调（数据入口）
 * 
 * 【核心逻辑】
 * 1. 收到 Link 层的帧指针
 * 2. 申请 Data Bus Item
 * 3. ** memcpy 拷贝数据 ** (关键！不能直接用 Link 的指针)
 * 4. 发布 Data Bus Item
 * 5. ** 立即归还 Link 层的帧 ** (关键！否则摄像头会饿死)
 */
static void _capture_srv_on_link_frame(const video_frame_t *frame, void *user_data)
{
    capture_srv_ctx_t *ctx = (capture_srv_ctx_t*)user_data;
    if (ctx == NULL || frame == NULL) return;

    // 检查状态
    module_state_t state = module_fsm_get_state(ctx->fsm_handle);
    if (state != MODULE_STATE_RUNNING) {
        return;
    }

    // 1. 从 Data Bus 申请 Item
    data_bus_item_handle_t item = NULL;
    size_t data_size = frame->length;
    
    int ret = data_bus_alloc(ctx->data_bus, 
                              DATA_TYPE_VIDEO_FRAME, 
                              data_size, 
                              "capture_srv", 
                              &item);
    if (ret != 0 || item == NULL) {
        // Data Bus 满了，这帧直接丢，必须归还 Link 帧！
        // 【注意】这里我们没有 capture_srv_put_frame 的句柄，
        // 但没关系，我们修改一下逻辑，不在这里还，
        // 而是修改 Link 层的回调机制，让它在回调返回后自动还！
        // 
        // 为了最小化改动，我们采用一个更简单的方案：
        // 【方案】修改 frame_link.c，在 _frame_link_enqueue 之后，
        // 或者在回调返回之后，由 Link 层自己负责把 HAL 层的帧还回去！
        return;
    }

    // 2. 拷贝数据 (从 Link 的 mmap 区域 拷贝到 Data Bus 的池子里)
    void *w_ptr = data_bus_get_writable_ptr(item);
    if (w_ptr == NULL) {
        data_bus_release(item);
        return;
    }
    memcpy(w_ptr, frame->data, data_size);

    // 3. 发布到 Data Bus
    ret = data_bus_publish(ctx->data_bus, item);
    if (ret != 0) {
        data_bus_release(item);
        return;
    }
    
    // 4. 生产者释放引用
    data_bus_release(item); 

    // 5. 发布事件到 Event Bus
    event_bus_publish_simple(ctx->evt_bus, EVENT_TYPE_CAP_FRAME_READY, "capture_srv");

    // 【重要】此时，数据已经安全拷贝到 Data Bus 了。
    // 但是！Link 层的那个 frame 还没还给摄像头！
    // 我们需要修改 Link 层的逻辑。
}

/**
 * @brief 【核心】推送到双总线
 */
static int _capture_srv_feed_bus(capture_srv_ctx_t *ctx, const video_frame_t *frame)
{
    if (ctx == NULL || frame == NULL) return -1;

    // 检查总线句柄
    if (ctx->data_bus == NULL || ctx->evt_bus == NULL) {
        return -1;
    }

    // 1. 从 Data Bus 申请 Item
    data_bus_item_handle_t item = NULL;
    size_t data_size = frame->length;
    
    int ret = data_bus_alloc(ctx->data_bus, 
                              DATA_TYPE_VIDEO_FRAME, 
                              data_size, 
                              "capture_srv", 
                              &item);
    if (ret != 0 || item == NULL) {
        return -1;
    }

    // 2. 拷贝数据
    void *w_ptr = data_bus_get_writable_ptr(item);
    if (w_ptr == NULL) {
        data_bus_release(item);
        return -1;
    }
    memcpy(w_ptr, frame->data, data_size);

    // 3. 发布到 Data Bus
    ret = data_bus_publish(ctx->data_bus, item);
    if (ret != 0) {
        data_bus_release(item);
        return -1;
    }
    
    // 【重要】发布成功后，生产者也要释放自己的引用！
    // 因为 data_bus_publish 后，所有权就交给总线了。
    // 如果不 release，ref_count 会一直 >= 1，导致 item 永远无法回收！
    data_bus_release(item); 

    // 4. 发布事件到 Event Bus
    event_bus_publish_simple(ctx->evt_bus, EVENT_TYPE_CAP_FRAME_READY, "capture_srv");

    return 0;
}