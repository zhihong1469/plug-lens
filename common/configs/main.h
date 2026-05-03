#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <termios.h>

// 总线
#include "event_bus.h"
#include "data_bus.h"

// 状态机
#include "global_fsm.h"
#include "module_fsm.h"

// 服务
#include "capture_srv.h"

// ==========================================================================
// 【全局根上下文】整个工程唯一的全局结构体，无全局变量
// ==========================================================================
typedef struct {
    // 核心总线
    event_bus_handle_t evt_bus;
    data_bus_handle_t data_bus;

    // 状态机
    global_fsm_handle_t g_fsm;

    // 业务服务
    capture_srv_handle_t cap_srv;

    // 退出通知管道（核心：替代全局 g_quit_flag）
    int exit_pipe[2];   // [0]=读端 [1]=写端

    // 终端
    struct termios old_tio;
    bool tio_saved;

    // 运行标志
    volatile sig_atomic_t quit_flag;
} app_context_t;

#endif