#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <termios.h>
#include "event_bus.h"
#include "data_bus.h"
#include "global_fsm.h"
#include "capture_srv.h"
#include "face_detect_srv.h"
// ==========================================================================
// 应用全局上下文：统一收口所有全局资源，彻底消灭零散全局变量
// 全工程唯一顶层上下文，src底层、plugins插件均可安全引用
// ==========================================================================
typedef struct {
    // 核心总线句柄
    event_bus_handle_t      evt_bus;
    data_bus_handle_t       data_bus;

    // 状态机 & 业务服务句柄
    global_fsm_handle_t     g_fsm;
    capture_srv_handle_t    cap_srv;
    face_detect_srv_handle_t face_detect_srv;
    // 优雅退出：内核管道（替代全局quit_flag，线程安全、事件驱动）
    int                     exit_pipe[2];     // [0]读端 / [1]写端

    // 终端模式保存（基础设施）
    struct termios          old_termios;
    bool                    termios_saved;

    // 全局运行标记（可选，作为兜底，优先用Pipe事件）
    volatile bool           app_running;
} app_context_t;

// ==========================================================================
// 全局唯一应用上下文实例（公共层统一实例化，无零散全局变量）
// ==========================================================================
extern app_context_t g_app_ctx;

// ==========================================================================
// 公共基建函数声明（main.c实现，插件可调用）
// ==========================================================================
// 初始化退出管道
int app_exit_pipe_init(void);
// 触发全局优雅退出（写管道，所有线程感知）
void app_trigger_soft_exit(void);
// 销毁退出管道
void app_exit_pipe_deinit(void);

// 终端基础设施
void app_set_terminal_noncanonical(void);
void app_restore_terminal_safe(void);

#endif /* MAIN_H */
