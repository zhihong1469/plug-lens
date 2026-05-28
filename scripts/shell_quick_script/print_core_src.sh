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
echo "--- [1/9] 应用入口层 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/app/main.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/configs/main.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/configs/vision_ai_config.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 2. 主线程交互（应用插件） =====================
echo "--- [2/9] 应用插件 - DemoApp ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/app_plugins/inc/demo_app.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/app_plugins/src/demo_app.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 3. 帧链路层（视频数据核心） =====================
echo "--- [3/9] 帧链路 - FrameLink（视频流处理） ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/link/frame_link/inc/frame_link.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/link/frame_link/src/frame_link.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 4. AI模型链路层 =====================
echo "--- [4/9] AI模型链路 - AiModelLink（MNN推理） ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/link/ai_model_link/inc/ai_model_link.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/link/ai_model_link/src/ai_model_link.cpp" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 5. 业务服务层（采集+人脸检测） =====================
echo "--- [5/9] 业务服务 - 采集服务+人脸检测服务 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/service/capture_srv/inc/capture_srv.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/service_plugins/capture_srv/src/capture_srv.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/service/face_detect_srv/inc/face_detect_srv.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/service_plugins/face_detect_srv/src/face_detect_srv.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 6. USB摄像头+MNN AI device驱动 =====================
echo "--- [6/9] 硬件插件 - USB摄像头V4L2 + MNN AI模型 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/device_plugins/video_usb/inc/video_usb.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/device_plugins/video_usb/src/video_usb.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/device_plugins/video_usb/src/video_device.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/device_plugins/ai_model_mnn/src/ai_model_mnn.cpp" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 7. 双总线（事件+数据） =====================
echo "--- [7/9] 核心组件 - 事件总线+数据总线 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/event_bus/inc/event_bus.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/event_bus/src/event_bus.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/data_bus/inc/data_bus.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/bus/data_bus/src/data_bus.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 8. 状态机FSM（全局+模块） =====================
echo "--- [8/9] 核心组件 - 全局+模块状态机 ---" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/global_fsm/inc/global_fsm.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/global_fsm/src/global_fsm.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/module_fsm/inc/module_fsm.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/fsm/module_fsm/src/module_fsm.c" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 9. 通用工具组件（项目核心依赖） =====================
echo "--- [9/9] 通用组件 - 日志/队列/线程/内存池/插件加载/图像处理 ---" >> "$OUTPUT_FILE"
# 基础组件
cat "$PROJECT_ROOT/common/log/inc/log.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/log/src/log.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/queue/inc/queue.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/queue/src/queue.c" >> "$OUTPUT_FILE"
# 新增核心组件
cat "$PROJECT_ROOT/common/thread/inc/thread.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/thread/src/thread.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/pool/inc/pool.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/pool/src/pool.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/plugin/inc/plugin_loader.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/plugin/src/plugin_loader.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/img_proc/inc/img_proc.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/img_proc/src/img_proc_c.c" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/utils/inc/utils.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/utils/src/utils.c" >> "$OUTPUT_FILE"
# device抽象接口
cat "$PROJECT_ROOT/src/device/video/inc/video_device.h" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/device/ai_model/inc/ai_model_mnn.hpp" >> "$OUTPUT_FILE"

# 完成提示
echo -e "\033[32m ✅ 核心源码已导出：$OUTPUT_FILE \033[0m"
echo -e "\033[32m ✅ 直接复制发给AI即可！ \033[0m"