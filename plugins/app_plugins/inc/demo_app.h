// plugins/app_plugins/demo_app/inc/demo_app.h
#ifndef DEMO_APP_H
#define DEMO_APP_H

#include "plugin_loader.h"

extern const plugin_desc_t g_demo_app_desc;

// 【新增】导出命令循环函数
void demo_app_command_loop(void);

#endif /* DEMO_APP_H */