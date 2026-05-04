#!/bin/bash
# 嵌入式Linux视觉项目 - 轻量级核心源码导出（仅导出开发必需文件）
# 脚本位置：scripts/print_core_src.sh
# 输出位置：scripts/core_project_code.txt
# 用法：chmod +x scripts/print_core_src.sh && ./scripts/print_core_src.sh

# 自动定位路径
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
OUTPUT_FILE="$SCRIPT_DIR/core_project_code.txt"

# 清空旧文件
> "$OUTPUT_FILE"

# 标题
echo "============ 嵌入式Linux视觉采集终端 - 核心源码 ============" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 1. 应用入口（核心） =====================
echo "--- [1/8] 应用入口层 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/app/main.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/configs/main.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/configs/vision_ai_config.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 2. 主线程交互（BUG修复核心） =====================
echo "--- [2/8] 应用插件 - DemoApp（select阻塞问题） ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/app_plugins/inc/demo_app.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/app_plugins/src/demo_app.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 3. 帧链路层（子线程核心） =====================
echo "--- [3/8] 帧链路 - FrameLink（子线程poll退出） ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/link/frame_link/inc/frame_link.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/link/frame_link/src/frame_link.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 4. 采集服务层 =====================
echo "--- [4/8] 业务服务 - CaptureSrv ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/service/capture_srv/inc/capture_srv.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/service_plugins/capture_srv/src/capture_srv.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 5. USB摄像头HAL驱动 =====================
echo "--- [5/8] 硬件插件 - USB摄像头V4L2 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/hal_plugins/video_usb/inc/video_usb.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/hal_plugins/video_usb/src/video_usb.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/hal_plugins/video_usb/src/video_hal.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 6. 双总线 =====================
echo "--- [6/8] 核心组件 - 事件总线+数据总线 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/event_bus/inc/event_bus.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/event_bus/src/event_bus.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/data_bus/inc/data_bus.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/data_bus/src/data_bus.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 7. 状态机FSM =====================
echo "--- [7/8] 核心组件 - 全局+模块状态机 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/global_fsm/inc/global_fsm.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/global_fsm/src/global_fsm.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/module_fsm/inc/module_fsm.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/module_fsm/src/module_fsm.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 8. 通用工具 =====================
echo "--- [8/8] 通用组件 - 日志/队列 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/log/inc/log.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/log/src/log.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/queue/inc/queue.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/queue/src/queue.c" >> "$OUTPUT_FILE"

# 完成提示
echo -e "\033[32m ✅ 核心源码已导出：$OUTPUT_FILE \033[0m"
echo -e "\033[32m ✅ 直接复制发给AI即可！ \033[0m"