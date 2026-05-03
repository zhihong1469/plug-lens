#!/bin/bash
# 嵌入式Linux视频AI项目 - 自动导出所有核心源码到【项目根目录】TXT文件
# 脚本位置：scripts/print_all_src.sh
# 输出位置：项目根目录/all_project_code.txt
# 用法：chmod +x scripts/print_all_src.sh && ./scripts/print_all_src.sh

# ===================== 核心路径配置（自动定位根目录） =====================
# 获取脚本所在目录
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# 项目根目录（脚本目录的上一级）
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
# 输出文件：项目根目录/all_project_code.txt
OUTPUT_FILE="$SCRIPT_DIR/all_project_code.txt"

# 清空旧文件
> "$OUTPUT_FILE"

# ===================== 开始写入 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "  嵌入式Linux视频AI视觉终端 - 完整源码合集" >> "$OUTPUT_FILE"
echo "  六层双总线架构 | 插件化设计 | ARM Linux" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 1. 顶层构建系统 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【构建系统】Makefile (顶层)" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/Makefile" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【构建系统】Makefile.build (通用编译规则)" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/Makefile.build" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 2. 公共组件层 common =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/Makefile" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/Makefile" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/configs/vision_ai_config.h (全局配置)" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/configs/vision_ai_config.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/log/inc/log.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/log/inc/log.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/log/src/log.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/log/src/log.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/queue/inc/queue.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/queue/inc/queue.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/queue/src/queue.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/queue/src/queue.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/plugin/inc/plugin_loader.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/plugin/inc/plugin_loader.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/plugin/src/plugin_loader.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/plugin/src/plugin_loader.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 3. HAL层通用接口 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【HAL层】src/hal/video/inc/video_hal.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/hal/video/inc/video_hal.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 4. 双总线中枢 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【总线】src/bus/event_bus/inc/event_bus.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/event_bus/inc/event_bus.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【总线】src/bus/event_bus/src/event_bus.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/event_bus/src/event_bus.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【总线】src/bus/data_bus/inc/data_bus.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/data_bus/inc/data_bus.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【总线】src/bus/data_bus/src/data_bus.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/data_bus/src/data_bus.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 5. 状态机层 FSM =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【FSM层】src/fsm/module_fsm/inc/module_fsm.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/module_fsm/inc/module_fsm.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【FSM层】src/fsm/module_fsm/src/module_fsm.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/module_fsm/src/module_fsm.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【FSM层】src/fsm/global_fsm/inc/global_fsm.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/global_fsm/inc/global_fsm.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【FSM层】src/fsm/global_fsm/src/global_fsm.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/global_fsm/src/global_fsm.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 6. 数据链路层 Link =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【Link层】src/link/frame_link/inc/frame_link.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/link/frame_link/inc/frame_link.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【Link层】src/link/frame_link/src/frame_link.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/link/frame_link/src/frame_link.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 7. 服务层 Service =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【服务层】src/service/capture_srv/inc/capture_srv.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/service/capture_srv/inc/capture_srv.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【服务层】src/service/capture_srv/src/capture_srv.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/service/capture_srv/src/capture_srv.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 8. 核心框架 Makefile =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【构建系统】src/Makefile" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/Makefile" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 9. 插件层 Plugins =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【插件层】plugins/Makefile" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/Makefile" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【硬件插件】plugins/hal_plugins/video_usb/inc/video_usb.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/hal_plugins/video_usb/inc/video_usb.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【硬件插件】plugins/hal_plugins/video_usb/src/video_hal.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/hal_plugins/video_usb/src/video_hal.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【硬件插件】plugins/hal_plugins/video_usb/src/video_usb.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/hal_plugins/video_usb/src/video_usb.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【业务插件】plugins/app_plugins/inc/demo_app.h" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/app_plugins/inc/demo_app.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【业务插件】plugins/app_plugins/src/demo_app.c" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/app_plugins/src/demo_app.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 10. 应用层入口 Main =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【应用层】src/app/main.c (系统主入口)" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/app/main.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 完成提示 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "  所有源码展示完成！" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"

# 终端彩色提示
echo -e "\033[32m ============================================== \033[0m"
echo -e "\033[32m ✅ 脚本路径：$SCRIPT_DIR/print_all_src.sh \033[0m"
echo -e "\033[32m ✅ 源码已导出到：$OUTPUT_FILE \033[0m"
echo -e "\033[32m ✅ 直接发送给AI即可！ \033[0m"
echo -e "\033[32m ============================================== \033[0m"