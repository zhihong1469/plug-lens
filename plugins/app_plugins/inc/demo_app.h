// plugins/app_plugins/inc/demo_app.h
#ifndef DEMO_APP_H
#define DEMO_APP_H   
#include "global_fsm.h"   
#include "module_fsm.h"
#include "event_bus.h"
#include "data_bus.h"
#include "global_fsm.h"
#include "capture_srv.h"
#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// Demo App 配置
// ==========================================================================
typedef struct {
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;
    global_fsm_handle_t g_fsm;
    capture_srv_handle_t cap_srv;
} demo_app_config_t;

// ==========================================================================
// 接口
// ==========================================================================

int demo_app_init(const demo_app_config_t *config);
int demo_app_run(void); // 主循环
int demo_app_deinit(void);

#endif /* DEMO_APP_H */