#!/bin/bash
# 嵌入式Linux视觉项目 - 核心源码导出脚本
# ==============================================
# 【🔥 手动配置区 - 仅需填写相对路径】
PROJECT_NAME="嵌入式LinuxAI人脸抓拍实时推流项目"  # 项目名称（仅用于输出文件标题）
# 1. 脚本 相对于 项目根目录的路径 (VSCode复制相对路径)
SCRIPT_REL_PATH="scripts/shell_quick_script/print_core.sh"

# 2. 输出文件 相对于 项目根目录的路径
OUTPUT_REL_PATH="scripts/shell_quick_script/tmp/core_project_code.txt"

# 3. 需要导出的文件列表（相对根目录路径，自由增删）
EXPORT_FILES=(
    # 应用入口
    "src/app/src/main.c"
    "common/configs/config_common.h"
    "common/configs/vision_ai_config.h"

    # 应用插件
    "plugins/app_plugins/src/demo_app.c"

    # 帧链路
    "src/link/frame_link/inc/frame_link.h"

    # 服务层
    "plugins/service_plugins/capture_srv/inc/capture_srv.h"
    "plugins/service_plugins/face_detect_srv/inc/face_detect_srv.h"

    # 硬件插件
    "plugins/base_plugins/camera_usb/inc/camera_usb.h"
    "plugins/base_plugins/ai_model_mnn/inc/ai_model_mnn.hpp"

    # 总线/状态机
    "src/bus/event_bus/inc/event_bus.h"
    "src/bus/data_bus/inc/data_bus.h"
    "src/fsm/global_fsm/inc/global_fsm.h"
    "src/fsm/module_fsm/inc/module_fsm.h"

    # 通用工具
    "common/log/inc/log.h"
    "common/queue/inc/queue.h"
    "common/thread/inc/thread.h"
    "common/pool/inc/pool.h"
    "common/plugin/inc/plugin_loader.h"
    "common/img_proc/inc/img_proc.h"
    "common/utils/inc/utils.h"

    # 基础抽象
    "src/base/camera/inc/camera_base.h"
    "src/base/ai_model/inc/ai_model_base.h"
)
# ==============================================

# ===================== 核心：自动计算路径（零报错） =====================
# 1. 获取脚本所在绝对目录
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# 2. 自动计算项目根目录（基于你的脚本相对路径，100%准确）
PROJECT_ROOT=$(cd "$SCRIPT_DIR/../../" && pwd)

# 3. 最终输出文件绝对路径
OUTPUT_FILE="$PROJECT_ROOT/$OUTPUT_REL_PATH"
OUTPUT_DIR=$(dirname "$OUTPUT_FILE")
# ======================================================================

# ===================== 功能1：自动创建输出目录 + 明确提示 =====================
if [ -d "$OUTPUT_DIR" ]; then
    echo -e "\033[34mℹ️  输出目录已存在：$OUTPUT_DIR\033[0m"
else
    mkdir -p "$OUTPUT_DIR"
    echo -e "\033[32m✅ 成功创建输出目录：$OUTPUT_DIR\033[0m"
fi

# 清空旧文件
> "$OUTPUT_FILE"

# ===================== 功能2：安全导出 + 精准提示缺失文件 =====================
export_file() {
    local REL_FILE=$1
    local ABS_FILE="$PROJECT_ROOT/$REL_FILE"
    
    # 写入文件标题
    echo "=====================================" >> "$OUTPUT_FILE"
    echo "📄 导出文件：$REL_FILE" >> "$OUTPUT_FILE"
    echo "=====================================" >> "$OUTPUT_FILE"

    # 检查文件是否存在
    if [ -f "$ABS_FILE" ]; then
        cat "$ABS_FILE" >> "$OUTPUT_FILE"
    else
        echo -e "\033[31m⚠️  缺失文件：$REL_FILE\033[0m"
        echo "❌ 文件不存在，跳过导出" >> "$OUTPUT_FILE"
    fi
    echo -e "\n\n" >> "$OUTPUT_FILE"
}

# ===================== 执行导出 =====================
# 写入文档标题
echo "============ $PROJECT_NAME - 核心接口源码 ============" >> "$OUTPUT_FILE"
echo "✅ 导出规范：优先导出头文件（对外接口）" >> "$OUTPUT_FILE"
echo "✅ 项目根目录：$PROJECT_ROOT" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# 遍历导出所有文件
echo -e "\033[34mℹ️  开始导出核心源码，共 ${#EXPORT_FILES[@]} 个文件...\033[0m"
for file in "${EXPORT_FILES[@]}"; do
    export_file "$file"
done

# ===================== 完成提示 =====================
echo -e "\033[32m==================================================\033[0m"
echo -e "\033[32m✅ 源码导出完成！\033[0m"
echo -e "\033[32m📂 项目根目录：\033[0m"
echo -e "\033[32m$PROJECT_ROOT \033[0m"
echo -e "\033[32m📄 输出文件：\033[0m"
echo -e "\033[32m$OUTPUT_FILE \033[0m"
echo -e "\033[32m==================================================\033[0m"