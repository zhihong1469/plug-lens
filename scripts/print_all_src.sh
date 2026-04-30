#!/bin/bash
# 嵌入式Linux视频AI终端 - 完整源代码打印脚本
# 按架构从底层到上层顺序打印，带清晰分隔符

echo "========================================"
echo "  开始打印完整源代码"
echo "========================================"
echo ""

# ========================================
# 1. 顶层构建系统
# ========================================
echo "========================================"
echo "  文件: Makefile（顶层）"
echo "========================================"
cat Makefile
echo ""
echo ""

echo "========================================"
echo "  文件: Makefile.build（通用编译规则）"
echo "========================================"
cat Makefile.build
echo ""
echo ""

# ========================================
# 2. 公共组件层（common）
# ========================================
echo "========================================"
echo "  文件: common/Makefile"
echo "========================================"
cat common/Makefile
echo ""
echo ""

echo "========================================"
echo "  文件: common/configs/vision_ai_config.h（统一配置）"
echo "========================================"
cat common/configs/vision_ai_config.h
echo ""
echo ""

echo "========================================"
echo "  文件: common/log/inc/log.h"
echo "========================================"
cat common/log/inc/log.h
echo ""
echo ""

echo "========================================"
echo "  文件: common/log/src/log.c"
echo "========================================"
cat common/log/src/log.c
echo ""
echo ""

echo "========================================"
echo "  文件: common/queue/inc/queue.h"
echo "========================================"
cat common/queue/inc/queue.h
echo ""
echo ""

echo "========================================"
echo "  文件: common/queue/src/queue.c"
echo "========================================"
cat common/queue/src/queue.c
echo ""
echo ""

echo "========================================"
echo "  文件: common/plugin/inc/plugin_loader.h"
echo "========================================"
cat common/plugin/inc/plugin_loader.h
echo ""
echo ""

echo "========================================"
echo "  文件: common/plugin/src/plugin_loader.c"
echo "========================================"
cat common/plugin/src/plugin_loader.c
echo ""
echo ""

# ========================================
# 3. HAL层通用接口（src/hal）
# ========================================
echo "========================================"
echo "  文件: src/hal/video/inc/video_hal.h"
echo "========================================"
cat src/hal/video/inc/video_hal.h
echo ""
echo ""

# ========================================
# 4. 双总线中枢（src/bus）
# ========================================
echo "========================================"
echo "  文件: src/bus/event_bus/inc/event_bus.h"
echo "========================================"
cat src/bus/event_bus/inc/event_bus.h
echo ""
echo ""

echo "========================================"
echo "  文件: src/bus/event_bus/src/event_bus.c"
echo "========================================"
cat src/bus/event_bus/src/event_bus.c
echo ""
echo ""

echo "========================================"
echo "  文件: src/bus/data_bus/inc/data_bus.h"
echo "========================================"
cat src/bus/data_bus/inc/data_bus.h
echo ""
echo ""

echo "========================================"
echo "  文件: src/bus/data_bus/src/data_bus.c"
echo "========================================"
cat src/bus/data_bus/src/data_bus.c
echo ""
echo ""

# ========================================
# 5. FSM层（src/fsm）
# ========================================
echo "========================================"
echo "  文件: src/fsm/module_fsm/inc/module_fsm.h"
echo "========================================"
cat src/fsm/module_fsm/inc/module_fsm.h
echo ""
echo ""

echo "========================================"
echo "  文件: src/fsm/module_fsm/src/module_fsm.c"
echo "========================================"
cat src/fsm/module_fsm/src/module_fsm.c
echo ""
echo ""

echo "========================================"
echo "  文件: src/fsm/global_fsm/inc/global_fsm.h"
echo "========================================"
cat src/fsm/global_fsm/inc/global_fsm.h
echo ""
echo ""

echo "========================================"
echo "  文件: src/fsm/global_fsm/src/global_fsm.c"
echo "========================================"
cat src/fsm/global_fsm/src/global_fsm.c
echo ""
echo ""

# ========================================
# 6. Link层（src/link）
# ========================================
echo "========================================"
echo "  文件: src/link/frame_link/inc/frame_link.h"
echo "========================================"
cat src/link/frame_link/inc/frame_link.h
echo ""
echo ""

echo "========================================"
echo "  文件: src/link/frame_link/src/frame_link.c"
echo "========================================"
cat src/link/frame_link/src/frame_link.c
echo ""
echo ""

# ========================================
# 7. Service层（src/service）
# ========================================
echo "========================================"
echo "  文件: src/service/capture_srv/inc/capture_srv.h"
echo "========================================"
cat src/service/capture_srv/inc/capture_srv.h
echo ""
echo ""

echo "========================================"
echo "  文件: src/service/capture_srv/src/capture_srv.c"
echo "========================================"
cat src/service/capture_srv/src/capture_srv.c
echo ""
echo ""

# ========================================
# 8. 核心框架Makefile（src）
# ========================================
echo "========================================"
echo "  文件: src/Makefile"
echo "========================================"
cat src/Makefile
echo ""
echo ""

# ========================================
# 9. 插件层（plugins）
# ========================================
echo "========================================"
echo "  文件: plugins/Makefile"
echo "========================================"
cat plugins/Makefile
echo ""
echo ""

echo "========================================"
echo "  文件: plugins/hal_plugins/video_usb/inc/video_usb.h"
echo "========================================"
cat plugins/hal_plugins/video_usb/inc/video_usb.h
echo ""
echo ""

echo "========================================"
echo "  文件: plugins/hal_plugins/video_usb/src/video_hal.c"
echo "========================================"
cat plugins/hal_plugins/video_usb/src/video_hal.c
echo ""
echo ""

echo "========================================"
echo "  文件: plugins/hal_plugins/video_usb/src/video_usb.c"
echo "========================================"
cat plugins/hal_plugins/video_usb/src/video_usb.c
echo ""
echo ""

echo "========================================"
echo "  文件: plugins/app_plugins/inc/demo_app.h"
echo "========================================"
cat plugins/app_plugins/inc/demo_app.h
echo ""
echo ""

echo "========================================"
echo "  文件: plugins/app_plugins/src/demo_app.c"
echo "========================================"
cat plugins/app_plugins/src/demo_app.c
echo ""
echo ""

# ========================================
# 10. App层入口（src/app）
# ========================================
echo "========================================"
echo "  文件: src/app/main.c"
echo "========================================"
cat src/app/main.c
echo ""
echo ""

echo "========================================"
echo "  完整源代码打印完成"
echo "========================================"