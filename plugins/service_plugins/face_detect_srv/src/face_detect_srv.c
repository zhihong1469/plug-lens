#include "face_detect_srv.h"
#include "log.h"
#include "main.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ==========================================================================
// 【核心】人脸检测服务专属：状态迁移规则表（1:1 复刻采集服务）
// ==========================================================================
static module_state_trans_t g_face_detect_srv_trans_table[] = {
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
static const uint32_t g_face_detect_srv_trans_len = 
    sizeof(g_face_detect_srv_trans_table) / sizeof(g_face_detect_srv_trans_table[0]);

// ==========================================================================
// 内部上下文结构体（完美封装）
// ==========================================================================
typedef struct {
    // 配置与回调
    face_detect_srv_config_t config;
    
    // 下层依赖（AI适配层）
    bool ai_initialized;
    
    // 子状态机
    module_fsm_handle_t fsm_handle;
    
    // 总线句柄（缓存）
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;
    
    // 数据总线订阅
    data_bus_subscription_t data_sub;
    bool data_subscribed;
    
    // 运行时状态
    bool is_created;
    pthread_mutex_t lock;
} face_detect_srv_ctx_t;

// ==========================================================================
// 内部辅助函数声明
// ==========================================================================
static int _face_detect_srv_fsm_event_handler(module_event_t event, void *user_data);
static void _face_detect_srv_fsm_state_relay(const char *module_name,
                                              module_state_t old_state,
                                              module_state_t new_state,
                                              void *user_data);
static void _face_detect_srv_on_data_bus_frame(data_bus_item_handle_t item, void *user_data);
static int _face_detect_srv_process_frame(face_detect_srv_ctx_t *ctx, const uint8_t *data, int w, int h);

// ==========================================================================
// 对外API实现
// ==========================================================================

int face_detect_srv_create(const face_detect_srv_config_t *config,
                           face_detect_srv_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return -1;
    }

    // 1. 分配并清零上下文
    face_detect_srv_ctx_t *ctx = (face_detect_srv_ctx_t*)malloc(sizeof(face_detect_srv_ctx_t));
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(face_detect_srv_ctx_t));

    // 2. 拷贝配置与回调
    memcpy(&ctx->config, config, sizeof(face_detect_srv_config_t));
    ctx->evt_bus = config->evt_bus;
    ctx->data_bus = config->data_bus;
    pthread_mutex_init(&ctx->lock, NULL);

    // 3. 初始化 Module FSM
    LOG_I("FaceDetect Srv: Creating Module FSM...");
    module_fsm_config_t fsm_cfg = {0};
    fsm_cfg.module_name = "face_detect_srv";
    fsm_cfg.trans_table = g_face_detect_srv_trans_table;
    fsm_cfg.trans_table_len = g_face_detect_srv_trans_len;
    fsm_cfg.event_handler = _face_detect_srv_fsm_event_handler;
    fsm_cfg.state_cb = _face_detect_srv_fsm_state_relay;
    fsm_cfg.user_data = ctx;

    int ret = module_fsm_create(&fsm_cfg, &ctx->fsm_handle);
    if (ret != 0) {
        LOG_E("FaceDetect Srv: Failed to create Module FSM");
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }

    // 4. 启动初始化流程
    ctx->is_created = true;
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT);

    // 5. 自动启动（如果配置了）
    if (ctx->config.auto_start) {
        LOG_I("FaceDetect Srv: Auto start enabled");
        module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START);
    }

    *out_handle = (face_detect_srv_handle_t)ctx;
    LOG_I("FaceDetect Srv: Created successfully");
    return 0;
}

module_fsm_handle_t face_detect_srv_get_fsm(face_detect_srv_handle_t handle)
{
    if (handle == NULL) return NULL;
    face_detect_srv_ctx_t *ctx = (face_detect_srv_ctx_t*)handle;
    return ctx->fsm_handle;
}

int face_detect_srv_destroy(face_detect_srv_handle_t handle)
{
    if (handle == NULL) return -1;
    face_detect_srv_ctx_t *ctx = (face_detect_srv_ctx_t*)handle;

    pthread_mutex_lock(&ctx->lock);

    if (!ctx->is_created) {
        pthread_mutex_unlock(&ctx->lock);
        return 0;
    }

    // 1. 取消数据总线订阅
    if (ctx->data_subscribed) {
        data_bus_unsubscribe(ctx->data_bus, &ctx->data_sub);
        ctx->data_subscribed = false;
    }

    // 2. 投 DEINIT 事件
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT);

    // 3. 释放AI模型
    if (ctx->ai_initialized) {
        ai_model_link_deinit();
        ctx->ai_initialized = false;
    }

    // 4. 投 DEINIT_OK
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT_OK);

    // 5. 销毁 FSM
    module_fsm_destroy(ctx->fsm_handle);

    ctx->is_created = false;
    pthread_mutex_unlock(&ctx->lock);

    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    
    LOG_I("FaceDetect Srv: Destroyed");
    return 0;
}

// ==========================================================================
// 【核心】内部辅助函数实现
// ==========================================================================

/**
 * @brief FSM 事件处理器（在锁内调用，只做决策，不做耗时操作）
 */
static int _face_detect_srv_fsm_event_handler(module_event_t event, void *user_data)
{
    face_detect_srv_ctx_t *ctx = (face_detect_srv_ctx_t*)user_data;
    if (ctx == NULL) return -1;

    LOG_I("FaceDetect Srv: FSM Event Handler received: %s", module_event_to_str(event));
    return 0; // 允许所有迁移
}

/**
 * @brief 【关键】FSM 状态中继回调
 * 
 * 职责链：
 * Module FSM -> 此回调 -> 1. 执行AI动作 (Init/Deinit AI模型)
 *                        -> 2. 订阅/取消数据总线
 *                        -> 3. 转发给上层回调 (Global FSM)
 */
static void _face_detect_srv_fsm_state_relay(const char *module_name,
                                              module_state_t old_state,
                                              module_state_t new_state,
                                              void *user_data)
{
    face_detect_srv_ctx_t *ctx = (face_detect_srv_ctx_t*)user_data;
    if (ctx == NULL) return;

    LOG_I("FaceDetect Srv: State changed: %s -> %s", 
          module_state_to_str(old_state), 
          module_state_to_str(new_state));

    // 1. 执行AI动作（根据新状态）
    if (new_state == MODULE_STATE_INITIALIZING) {
        LOG_I("FaceDetect Srv: Initializing AI model...");
        int ret = ai_model_link_init(ctx->config.model_path,
                                       ctx->config.ai_input_w,
                                       ctx->config.ai_input_h,
                                       ctx->config.score_threshold, // 新增
                                       ctx->config.iou_threshold);  // 新增
        if (ret == MNN_FACE_OK) {
            ctx->ai_initialized = true;
            LOG_I("FaceDetect Srv: AI model initialized successfully");
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT_OK);
        } else {
            LOG_E("FaceDetect Srv: Failed to init AI model (err=%d)", ret);
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT_FAIL);
        }
    }
    else if (new_state == MODULE_STATE_STARTING) {
        LOG_I("FaceDetect Srv: Subscribing to Data Bus...");
        // 订阅数据总线的视频帧
        int ret = data_bus_subscribe(ctx->data_bus,
                                     DATA_TYPE_VIDEO_FRAME,
                                     _face_detect_srv_on_data_bus_frame,
                                     ctx,
                                     &ctx->data_sub);
        if (ret == 0) {
            ctx->data_subscribed = true;
            LOG_I("FaceDetect Srv: Data Bus subscribed");
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_OK);
        } else {
            LOG_E("FaceDetect Srv: Failed to subscribe Data Bus");
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_FAIL);
        }
    }
    else if (new_state == MODULE_STATE_STOPPING) {
        LOG_I("FaceDetect Srv: Unsubscribing from Data Bus...");
        if (ctx->data_subscribed) {
            data_bus_unsubscribe(ctx->data_bus, &ctx->data_sub);
            ctx->data_subscribed = false;
        }
        module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_STOP_OK);
    }

    // 2. 【重要】转发给上层回调（Global FSM）
    if (ctx->config.callbacks.state_change_cb != NULL) {
        ctx->config.callbacks.state_change_cb(module_name, old_state, new_state, 
                                               ctx->config.callbacks.user_data);
    }
}

/**
 * @brief 数据总线帧就绪回调（数据入口）
 * 
 * 【核心逻辑】
 * 1. 收到 Data Bus 的视频帧
 * 2. 提取 YUYV 数据
 * 3. 调用 AI 推理
 * 4. 发布检测结果到 Event Bus
 */
static void _face_detect_srv_on_data_bus_frame(data_bus_item_handle_t item, void *user_data)
{
    face_detect_srv_ctx_t *ctx = (face_detect_srv_ctx_t*)user_data;
    if (ctx == NULL || item == NULL) return;

    module_state_t state = module_fsm_get_state(ctx->fsm_handle);
    if (state != MODULE_STATE_RUNNING) {
        return;
    }

    // 获取数据
    const void* data = data_bus_get_readable_ptr(item);
    if (data == NULL) {
        return;
    }

    // 处理帧（这里假设摄像头分辨率是 CONFIG_CAPTURE_WIDTH x CONFIG_CAPTURE_HEIGHT）
    // 实际项目中可以从 data_bus_item 的 metadata 获取分辨率
    int ret = _face_detect_srv_process_frame(ctx, 
                                              (const uint8_t*)data, 
                                              CONFIG_CAPTURE_WIDTH, 
                                              CONFIG_CAPTURE_HEIGHT);
    if (ret != 0) {
        LOG_W("FaceDetect Srv: Failed to process frame");
    }
}

/**
 * @brief 【核心】处理单帧图像
 */
static int _face_detect_srv_process_frame(face_detect_srv_ctx_t *ctx, const uint8_t *data, int w, int h)
{
    if (ctx == NULL || data == NULL) return -1;

    FaceInfo_C faces[10];
    int face_num = 0;
    int ret = ai_model_link_infer(data, w, h, faces, 10, &face_num);
    if (ret != MNN_FACE_OK) {
        return -1;
    }

    // 【关键修改】统一使用 ai_model_link_* 函数名
    int ai_w, ai_h;
    ai_model_link_get_ai_size(&ai_w, &ai_h); // 原来是 ultra_face_get_ai_size
    for (int i = 0; i < face_num; i++) {
        ai_model_link_map_face(&faces[i], ai_w, ai_h, w, h); // 原来是 ultra_face_map_face
    }

    LOG_I("FaceDetect Srv: Detected %d face(s)", face_num);
    for (int i = 0; i < face_num; i++) {
        LOG_D("FaceDetect Srv: Face %d: (%.1f, %.1f)-(%.1f, %.1f), score=%.2f",
              i, faces[i].x1, faces[i].y1, faces[i].x2, faces[i].y2, faces[i].score);
    }

    if (face_num > 0) {
        event_bus_publish_simple(ctx->evt_bus, EVENT_TYPE_FACE_DETECTED, "face_detect_srv");
    }

    return 0;
}