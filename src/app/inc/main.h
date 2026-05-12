#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <termios.h>
#include "event_bus.h"
#include "data_bus.h"

// ==========================================================================
// 极简全局上下文：仅保留系统底层资源，无任何业务变量
// 对外隐藏，不暴露给子服务，彻底杜绝全局变量滥用
// ==========================================================================
typedef struct {
    // 核心总线句柄（全局唯一，底层必需）
    event_bus_handle_t      evt_bus;
    data_bus_handle_t       data_bus;

    // 系统级优雅退出管道（线程/信号安全）
    int                     exit_pipe[2];

    // 终端配置（调试用）
    struct termios          old_termios;
    bool                    termios_saved;

    // 全局运行标记
    volatile bool           app_running;
} app_context_t;

// ==========================================================================
// 公共底层接口（仅 main.c 内部使用，子服务无需调用）
// ==========================================================================
// 触发全局安全退出（信号/异常时调用）
void app_trigger_soft_exit(void);

// 获取全局上下文（仅Demo/框架使用，业务服务禁止调用）
app_context_t* app_get_context(void);

#endif /* MAIN_H */