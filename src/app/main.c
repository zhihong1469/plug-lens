// src/app/main.c
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "log.h"
#include "event_bus.h"
#include "data_bus.h"
#include "global_fsm.h"
#include "module_fsm.h"
#include "capture_srv.h"
#include "demo_app.h"
#include "vision_ai_config.h" 

// ==========================================================================
// 全局句柄（简化版，实际项目中建议封装在 main_context_t 里）
// ==========================================================================
event_bus_handle_t g_evt_bus = NULL;
data_bus_handle_t g_data_bus = NULL;
global_fsm_handle_t g_g_fsm = NULL;
capture_srv_handle_t g_cap_srv = NULL;
static volatile sig_atomic_t g_quit_flag = 0;

// ==========================================================================
// 信号处理
// ==========================================================================
static void _sigint_handler(int sig)
{
    (void)sig;
    g_quit_flag = 1;
    LOG_W("Main: Received SIGINT, shutting down...");
}

// ==========================================================================
// 【核心】Global FSM 回调 -> Event Bus 适配层
// ==========================================================================
static void _main_on_g_fsm_state_change(global_state_t old_state,
                                          global_state_t new_state,
                                          void *user_data)
{
    (void)user_data;
    
    // 全局状态变化，发布到 Event Bus
    if (g_evt_bus != NULL) {
        event_t evt = {0};
        evt.type = EVENT_TYPE_SYS_STATE_CHANGED;
        evt.source = "global_fsm";
        evt.data = &new_state;
        evt.data_len = sizeof(new_state);
        event_bus_publish(g_evt_bus, &evt);
    }
}

static void _main_on_g_fsm_event(global_event_t event,
                                  const char *module_name,
                                  void *user_data)
{
    (void)user_data;
    
    // 全局事件，转换后发布到 Event Bus
    if (g_evt_bus == NULL) return;

    event_type_t evt_type = EVENT_TYPE_INVALID;
    switch (event) {
        case GLOBAL_EVENT_MODULE_READY:   evt_type = EVENT_TYPE_MOD_READY; break;
        case GLOBAL_EVENT_MODULE_RUNNING: evt_type = EVENT_TYPE_MOD_RUNNING; break;
        case GLOBAL_EVENT_MODULE_ERROR:   evt_type = EVENT_TYPE_MOD_ERROR; break;
        default: break;
    }
    
    if (evt_type != EVENT_TYPE_INVALID) {
        event_bus_publish_simple(g_evt_bus, evt_type, module_name);
    }
}

// ==========================================================================
// 主函数
// ==========================================================================
int main(int argc, char **argv)
{
    int ret = 0;

    // 1. 初始化日志
    log_init(LOG_LEVEL_INFO);
    LOG_I("Main: ========================================");
    LOG_I("Main: Vision AI Application Starting...");
    LOG_I("Main: ========================================");

    // 2. 注册信号
    signal(SIGINT, _sigint_handler);

    // -------------------------------------------------------------------------
    // 3. 初始化双总线
    // -------------------------------------------------------------------------
    LOG_I("Main: Initializing Event Bus...");
    event_bus_config_t evt_bus_cfg = {0};
    evt_bus_cfg.max_subscribers = CONFIG_EVENT_BUS_MAX_SUBSCRIBERS;
    ret = event_bus_init(&evt_bus_cfg, &g_evt_bus);
    if (ret != 0) {
        LOG_E("Main: Failed to init Event Bus");
        goto error_exit;
    }

    LOG_I("Main: Initializing Data Bus...");
    data_bus_config_t data_bus_cfg = {0};
    data_bus_cfg.max_items = CONFIG_DATA_BUS_MAX_FRAMES;
    data_bus_cfg.max_item_size = 4 * 1024 * 1024; // 4MB
    ret = data_bus_init(&data_bus_cfg, &g_data_bus);
    if (ret != 0) {
        LOG_E("Main: Failed to init Data Bus");
        goto error_exit;
    }

    // -------------------------------------------------------------------------
    // 4. 初始化 Global FSM
    // -------------------------------------------------------------------------
    LOG_I("Main: Initializing Global FSM...");
    global_fsm_config_t g_fsm_cfg = {0};
    g_fsm_cfg.max_modules = CONFIG_GLOBAL_FSM_MAX_MODULES;
    g_fsm_cfg.state_cb = _main_on_g_fsm_state_change;
    g_fsm_cfg.event_cb = _main_on_g_fsm_event;
    g_fsm_cfg.user_data = NULL;
    ret = global_fsm_init(&g_fsm_cfg, &g_g_fsm);
    if (ret != 0) {
        LOG_E("Main: Failed to init Global FSM");
        goto error_exit;
    }

    // -------------------------------------------------------------------------
    // 5. 初始化业务服务（Capture Service）（【修复2】修正回调注入）
    // -------------------------------------------------------------------------
    LOG_I("Main: Initializing Capture Service...");
    
    // 【核心】构建 Capture Service 配置
    capture_srv_config_t cap_srv_cfg = {0};
    
    // 5.1 填充 Link层 配置（使用全局配置）
    cap_srv_cfg.link_cfg.hal_config.dev_path = CONFIG_CAPTURE_DEV_PATH;
    cap_srv_cfg.link_cfg.hal_config.width = CONFIG_CAPTURE_WIDTH;
    cap_srv_cfg.link_cfg.hal_config.height = CONFIG_CAPTURE_HEIGHT;
    cap_srv_cfg.link_cfg.hal_config.fps = CONFIG_CAPTURE_FPS;
    cap_srv_cfg.link_cfg.hal_config.format = CONFIG_CAPTURE_FORMAT;
    cap_srv_cfg.link_cfg.hal_config.buf_count = CONFIG_CAPTURE_BUF_COUNT;
    cap_srv_cfg.link_cfg.hal_config.lock_exposure = CONFIG_CAPTURE_LOCK_EXPOSURE;
    cap_srv_cfg.link_cfg.hal_config.lock_white_balance = CONFIG_CAPTURE_LOCK_WHITE_BALANCE;
    cap_srv_cfg.link_cfg.hal_config.lock_gain = CONFIG_CAPTURE_LOCK_GAIN;
    cap_srv_cfg.link_cfg.frame_pool_size = CONFIG_FRAME_LINK_POOL_SIZE;
    cap_srv_cfg.link_cfg.queue_size = CONFIG_FRAME_LINK_QUEUE_SIZE;
    
    // 5.2 注入双总线句柄
    cap_srv_cfg.evt_bus = g_evt_bus;
    cap_srv_cfg.data_bus = g_data_bus;
    
    // 5.3 【关键修复】完美注入 Global FSM 回调
    // 【删除】原来的 NULL 覆盖逻辑
    cap_srv_cfg.callbacks.state_change_cb = global_fsm_on_module_state_change;
    cap_srv_cfg.callbacks.user_data = g_g_fsm; // user_data 传 Global FSM 句柄
    cap_srv_cfg.auto_start = false;

    // 创建 Capture Service
    ret = capture_srv_create(&cap_srv_cfg, &g_cap_srv);
    if (ret != 0) {
        LOG_E("Main: Failed to create Capture Service");
        goto error_exit;
    }

    // 注册到 Global FSM
    module_fsm_handle_t cap_fsm = capture_srv_get_fsm(g_cap_srv);
    global_fsm_register_module(g_g_fsm, "capture_srv", cap_fsm, true);


    // -------------------------------------------------------------------------
    // 6. 初始化 Demo App
    // -------------------------------------------------------------------------
    LOG_I("Main: Initializing Demo App...");
    demo_app_config_t app_cfg = {0};
    app_cfg.evt_bus = g_evt_bus;
    app_cfg.data_bus = g_data_bus;
    app_cfg.g_fsm = g_g_fsm;
    app_cfg.cap_srv = g_cap_srv;
    ret = demo_app_init(&app_cfg);
    if (ret != 0) {
        LOG_E("Main: Failed to init Demo App");
        goto error_exit;
    }

    // -------------------------------------------------------------------------
    // 7. 主循环
    // -------------------------------------------------------------------------
    LOG_I("Main: Entering main loop...");
    demo_app_run();

    // -------------------------------------------------------------------------
    // 8. 优雅退出
    // -------------------------------------------------------------------------
    LOG_I("Main: Starting graceful shutdown...");

    demo_app_deinit();
    capture_srv_destroy(g_cap_srv);
    global_fsm_deinit(g_g_fsm);
    data_bus_deinit(g_data_bus);
    event_bus_deinit(g_evt_bus);

    LOG_I("Main: ========================================");
    LOG_I("Main: Application exited successfully");
    LOG_I("Main: ========================================");
    log_deinit();
    return 0;

error_exit:
    LOG_E("Main: ========================================");
    LOG_E("Main: Application exited with error");
    LOG_E("Main: ========================================");
    
    // 尝试清理
    if (g_cap_srv) capture_srv_destroy(g_cap_srv);
    if (g_g_fsm) global_fsm_deinit(g_g_fsm);
    if (g_data_bus) data_bus_deinit(g_data_bus);
    if (g_evt_bus) event_bus_deinit(g_evt_bus);
    log_deinit();
    return -1;
}