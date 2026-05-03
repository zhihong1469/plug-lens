#ifndef DEMO_APP_H
#define DEMO_APP_H   
#include "global_fsm.h"   
#include "module_fsm.h"
#include "event_bus.h"
#include "data_bus.h"
#include "capture_srv.h"
#include "main.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// Demo App 配置：完全依赖顶层上下文 + 私有参数，无任何全局引用
// ==========================================================================
typedef struct {
    // 核心句柄
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;
    global_fsm_handle_t g_fsm;
    capture_srv_handle_t cap_srv;

    // 优雅退出：管道读端（从顶层上下文注入）
    int exit_pipe_read_fd;
} demo_app_config_t;

// ==========================================================================
// 对外接口
// ==========================================================================
int demo_app_init(const demo_app_config_t *config);
int demo_app_run(void);  // 主循环（纯业务控制，基建全部剥离）
int demo_app_deinit(void);

#endif /* DEMO_APP_H */