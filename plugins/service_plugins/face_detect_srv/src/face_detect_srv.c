#include "face_detect_srv.h"
#include "log.h"
#include "main.h"
#include "ai_model_link.h"
#include "data_bus.h"
#include "event_bus.h"
#include "module_fsm.h"
#include "thread.h"   // 你的公共线程库
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 视频帧元数据（和采集服务对齐）
typedef struct {
    int width;
    int height;
    int format;
    int stride;
} video_frame_meta_t;

// ==========================================================================
// 状态迁移表
// ==========================================================================
static module_state_trans_t g_face_detect_srv_trans_table[] = {
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
static const uint32_t g_face_detect_srv_trans_len =
    sizeof(g_face_detect_srv_trans_table) / sizeof(module_state_trans_t);

// ==========================================================================
// 内部上下文（适配公共线程）
// ==========================================================================
typedef struct {
    face_detect_srv_config_t config;
    bool ai_initialized;
    module_fsm_handle_t fsm_handle;
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;

    data_bus_subscription_t data_sub;
    bool data_subscribed;
    int event_sub_id;
    bool event_subscribed;

    bool is_created;
    pthread_mutex_t lock;

    // 公共线程组件
    thread_t ai_thread;               // 线程句柄
    bool thread_running;              // 线程运行标志
    bool has_new_frame;               // 新帧就绪标志
    pthread_mutex_t frame_lock;       // 标志位线程安全锁
} face_detect_srv_ctx_t;

// ==========================================================================
// 内部函数声明
// ==========================================================================
static void* _face_detect_async_thread(void *arg);  // 严格匹配线程API入口格式
static int _face_detect_srv_fsm_event_handler(module_event_t event, void *user_data);
static void _face_detect_srv_fsm_state_relay(const char *module_name,
                                              module_state_t old_state,
                                              module_state_t new_state,
                                              void *user_data);
static void _face_detect_srv_event_cb(const event_t *event, void *user_data);
static void _face_detect_srv_data_cb(data_bus_item_handle_t item, void *user_data);
static int _face_detect_process_frame(face_detect_srv_ctx_t *ctx, const uint8_t *data,
                                      int w, int h);

// ==========================================================================
// 对外API
// ==========================================================================
int face_detect_srv_create(const face_detect_srv_config_t *config,
                           face_detect_srv_handle_t *out_handle)
{
    if (!config || !out_handle || !config->evt_bus || !config->data_bus) {
        LOG_E("FaceDetect: 无效参数");
        return -1;
    }

    face_detect_srv_ctx_t *ctx = calloc(1, sizeof(face_detect_srv_ctx_t));
    if (!ctx) return -1;

    // 初始化基础资源
    memcpy(&ctx->config, config, sizeof(face_detect_srv_config_t));
    ctx->evt_bus = config->evt_bus;
    ctx->data_bus = config->data_bus;
    pthread_mutex_init(&ctx->lock, NULL);
    pthread_mutex_init(&ctx->frame_lock, NULL);
    ctx->has_new_frame = false;
    ctx->thread_running = false;

    // 初始化状态机
    module_fsm_config_t fsm_cfg = {0};
    fsm_cfg.module_name = "face_detect_srv";
    fsm_cfg.trans_table = g_face_detect_srv_trans_table;
    fsm_cfg.trans_table_len = g_face_detect_srv_trans_len;
    fsm_cfg.event_handler = _face_detect_srv_fsm_event_handler;
    fsm_cfg.state_cb = _face_detect_srv_fsm_state_relay;
    fsm_cfg.user_data = ctx;

    if (module_fsm_create(&fsm_cfg, &ctx->fsm_handle) != 0) {
        LOG_E("FaceDetect: 状态机创建失败");
        pthread_mutex_destroy(&ctx->frame_lock);
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }

    ctx->is_created = true;
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT);

    // 自动启动
    if (ctx->config.auto_start) {
        module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START);
    }

    *out_handle = ctx;
    LOG_I("FaceDetect: 服务创建成功");
    return 0;
}

module_fsm_handle_t face_detect_srv_get_fsm(face_detect_srv_handle_t handle)
{
    return handle ? ((face_detect_srv_ctx_t*)handle)->fsm_handle : NULL;
}

int face_detect_srv_destroy(face_detect_srv_handle_t handle)
{
    if (!handle) return -1;
    face_detect_srv_ctx_t *ctx = handle;

    pthread_mutex_lock(&ctx->lock);
    if (!ctx->is_created) {
        pthread_mutex_unlock(&ctx->lock);
        return 0;
    }

    // 1. 安全停止AI线程（最高优先级）
    if (ctx->thread_running) {
        ctx->thread_running = false;
        thread_join(&ctx->ai_thread, NULL);
    }

    // 2. 取消事件总线订阅
    if (ctx->event_subscribed) {
        event_bus_unsubscribe(ctx->evt_bus, ctx->event_sub_id);
        ctx->event_subscribed = false;
    }

    // 3. 取消数据总线订阅
    if (ctx->data_subscribed) {
        data_bus_unsubscribe(ctx->data_bus, &ctx->data_sub);
        ctx->data_subscribed = false;
    }

    // 4. 释放AI模型
    if (ctx->ai_initialized) {
        ai_model_link_deinit();
        ctx->ai_initialized = false;
    }

    // 状态机销毁流程
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT);
    module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_DEINIT_OK);
    module_fsm_destroy(ctx->fsm_handle);

    ctx->is_created = false;
    pthread_mutex_unlock(&ctx->lock);

    // 销毁锁资源
    pthread_mutex_destroy(&ctx->frame_lock);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    LOG_I("FaceDetect: 服务销毁完成");
    return 0;
}

// ==========================================================================
// 内部实现
// ==========================================================================
static int _face_detect_srv_fsm_event_handler(module_event_t event, void *user_data)
{
    return 0;
}

// AI异步处理线程（严格匹配你的线程API格式）
static void* _face_detect_async_thread(void *arg)
{
    face_detect_srv_ctx_t *ctx = (face_detect_srv_ctx_t*)arg;
    LOG_I("FaceDetect: AI异步线程启动");

    // 退出规则：全局退出 > 模块运行标志（对齐采集服务）
    while (g_app_ctx.app_running && ctx->thread_running)
    {
        bool need_process = false;

        // 线程安全读取新帧标志
        pthread_mutex_lock(&ctx->frame_lock);
        if (ctx->has_new_frame) {
            need_process = true;
            ctx->has_new_frame = false;
        }
        pthread_mutex_unlock(&ctx->frame_lock);

        if (!need_process) {
            thread_sleep_ms(10);  // 使用公共线程休眠API
            continue;
        }

        // 仅运行状态下处理
        if (module_fsm_get_state(ctx->fsm_handle) != MODULE_STATE_RUNNING) {
            continue;
        }

        // 从DataBus获取最新帧
        data_bus_item_handle_t item = NULL;
        if (data_bus_acquire_latest(ctx->data_bus, DATA_TYPE_VIDEO_FRAME, &item) == 0)
        {
            const uint8_t *data = data_bus_get_readonly_ptr(item);
            if (data) {
                _face_detect_process_frame(ctx, data, CONFIG_CAPTURE_WIDTH, CONFIG_CAPTURE_HEIGHT);
            }
            data_bus_release(item);
        }
    }

    LOG_I("FaceDetect: AI异步线程退出");
    return NULL;
}

// 状态机回调（线程创建/销毁由状态机管控）
static void _face_detect_srv_fsm_state_relay(const char *module_name,
                                              module_state_t old_state,
                                              module_state_t new_state,
                                              void *user_data)
{
    face_detect_srv_ctx_t *ctx = user_data;
    if (!ctx) return;

    LOG_I("FaceDetect: %s -> %s", module_state_to_str(old_state), module_state_to_str(new_state));

    // 1. 初始化：加载AI模型 + 订阅事件总线
    if (new_state == MODULE_STATE_INITIALIZING) {
        int ret = ai_model_link_init(ctx->config.model_path,
                                    ctx->config.ai_input_w,
                                    ctx->config.ai_input_h,
                                    ctx->config.score_threshold,
                                    ctx->config.iou_threshold);
        if (ret == MNN_FACE_OK) {
            ctx->ai_initialized = true;
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT_OK);

            // 订阅事件总线
            event_subscriber_t sub = {0};
            sub.event_type = EVENT_TYPE_INVALID;
            sub.callback = _face_detect_srv_event_cb;
            sub.user_data = ctx;
            ctx->event_sub_id = event_bus_subscribe(ctx->evt_bus, &sub);
            ctx->event_subscribed = (ctx->event_sub_id > 0);
            LOG_I("FaceDetect: 事件总线订阅成功");
        } else {
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_INIT_FAIL);
        }
    }

    // 2. 启动：订阅数据总线 + 创建AI线程
    if (new_state == MODULE_STATE_STARTING) {
        int ret = data_bus_subscribe(ctx->data_bus,
                                    DATA_TYPE_VIDEO_FRAME,
                                    _face_detect_srv_data_cb,
                                    ctx,
                                    &ctx->data_sub);
        if (ret == 0) {
            ctx->data_subscribed = true;

            // 初始化线程属性
            thread_attr_t attr;
            thread_attr_init(&attr);
            attr.name = "ai_thread";
            attr.stack_size = 256 * 1024;    // 256KB栈
            attr.priority = THREAD_PRIORITY_NORMAL; // 普通优先级
            attr.joinable = true;            // 可等待退出
            attr.detached = false;

            // 创建线程
            ctx->thread_running = true;
            thread_err_t terr = thread_create(&ctx->ai_thread, &attr, _face_detect_async_thread, ctx);
            if (terr == THREAD_OK) {
                module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_OK);
                event_bus_publish_simple(ctx->evt_bus, EVENT_TYPE_AI_START, "face_detect_srv");
                LOG_I("FaceDetect: AI线程创建成功");
            } else {
                LOG_E("FaceDetect: AI线程创建失败");
                ctx->thread_running = false;
                module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_FAIL);
            }
        } else {
            module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_START_FAIL);
        }
    }

    // 3. 停止：销毁线程 + 取消订阅
    if (new_state == MODULE_STATE_STOPPING) {
        // 停止线程
        if (ctx->thread_running) {
            ctx->thread_running = false;
            thread_join(&ctx->ai_thread, NULL);
        }

        // 取消数据订阅
        if (ctx->data_subscribed) {
            data_bus_unsubscribe(ctx->data_bus, &ctx->data_sub);
            ctx->data_subscribed = false;
        }

        module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_STOP_OK);
        event_bus_publish_simple(ctx->evt_bus, EVENT_TYPE_AI_STOP, "face_detect_srv");
    }

    // 发布状态事件
    event_bus_publish_simple(ctx->evt_bus, EVENT_TYPE_MOD_STATE_CHANGED, "face_detect_srv");

    // 上层回调
    if (ctx->config.callbacks.state_change_cb) {
        ctx->config.callbacks.state_change_cb(module_name, old_state, new_state,
                                             ctx->config.callbacks.user_data);
    }
}

// 事件总线回调
static void _face_detect_srv_event_cb(const event_t *event, void *user_data)
{
    face_detect_srv_ctx_t *ctx = user_data;
    if (!ctx || !event) return;

    if (event->type == EVENT_TYPE_SYS_STOP) {
        LOG_I("FaceDetect: 收到系统停止事件");
        module_fsm_post_event(ctx->fsm_handle, MODULE_EVENT_STOP);
    }
}

// 数据总线回调（仅置标志，0耗时不阻塞）
static void _face_detect_srv_data_cb(data_bus_item_handle_t item, void *user_data)
{
    face_detect_srv_ctx_t *ctx = user_data;
    if (!ctx || !item) return;

    // 状态校验
    if (module_fsm_get_state(ctx->fsm_handle) != MODULE_STATE_RUNNING) {
        data_bus_release(item);
        return;
    }

    // 标记新帧就绪
    pthread_mutex_lock(&ctx->frame_lock);
    ctx->has_new_frame = true;
    pthread_mutex_unlock(&ctx->frame_lock);

    data_bus_release(item);
}

// AI推理核心逻辑（无修改）
static int _face_detect_process_frame(face_detect_srv_ctx_t *ctx, const uint8_t *data, int w, int h)
{
    FaceInfo_C faces[10];
    int face_num = 0;

    int ret = ai_model_link_infer(data, w, h, faces, 10, &face_num);
    if (ret != MNN_FACE_OK) return -1;

    // 坐标映射
    int ai_w, ai_h;
    ai_model_link_get_ai_size(&ai_w, &ai_h);
    for (int i = 0; i < face_num; i++) {
        ai_model_link_map_face(&faces[i], ai_w, ai_h, w, h);
    }

    LOG_I("FaceDetect: 检测到 %d 张人脸", face_num);

    // 发布结果事件
    if (face_num > 0) {
        event_bus_publish_simple(ctx->evt_bus, EVENT_TYPE_FACE_DETECTED, "face_detect_srv");
    }
    event_bus_publish_simple(ctx->evt_bus, EVENT_TYPE_AI_RESULT_READY, "face_detect_srv");

    return 0;
}