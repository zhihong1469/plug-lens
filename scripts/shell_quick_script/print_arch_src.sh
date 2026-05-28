#!/bin/bash
# 嵌入式Linux视频AI项目 - 导出【工程架构/构建系统】文件
# ==============================================
# 【🔥 纯手动配置区 - 想导什么就写什么】
# 1. 脚本相对项目根目录的路径
SCRIPT_REL_PATH="scripts/shell_quick_script/print_arch_src.sh"
# 2. 输出文件相对根目录的路径
OUTPUT_REL_PATH="scripts/shell_quick_script/tmp/project_arch_code.txt"
# 3. 【手动指定】要导出的架构文件（相对根目录路径，自由增删）
EXPORT_FILES=(
    "Makefile"
    "Makefile.build"
    "common/Makefile"
    "common/configs/config_common.h"
    "common/configs/vision_ai_config.h"
    "src/Makefile"
    "plugins/Makefile"
)
# ==============================================

# ===================== 自动路径计算（无需修改） =====================
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/../../" && pwd)
OUTPUT_FILE="$PROJECT_ROOT/$OUTPUT_REL_PATH"
OUTPUT_DIR=$(dirname "$OUTPUT_FILE")
# ========================================================

# 自动创建输出目录 + 明确提示
if [ -d "$OUTPUT_DIR" ]; then
    echo -e "\033[34mℹ️  输出目录已存在：$OUTPUT_DIR\033[0m"
else
    mkdir -p "$OUTPUT_DIR"
    echo -e "\033[32m✅ 成功创建输出目录：$OUTPUT_DIR\033[0m"
fi

# 清空旧文件
> "$OUTPUT_FILE"

# ===================== 安全导出函数（修复完成） =====================
export_file() {
    local REL_FILE=$1
    local ABS_FILE="$PROJECT_ROOT/$REL_FILE"
    
    # 写入文件标题
    echo "=============================================" >> "$OUTPUT_FILE"
    echo "📄 导出文件：$REL_FILE" >> "$OUTPUT_FILE"
    echo "  绝对路径：$ABS_FILE" >> "$OUTPUT_FILE"
    echo "=============================================" >> "$OUTPUT_FILE"

    # 检查并写入文件内容
    if [ -f "$ABS_FILE" ]; then
        cat "$ABS_FILE" >> "$OUTPUT_FILE"
    else
        echo -e "\033[31m⚠️  缺失文件：$ABS_FILE\033[0m"
        echo "❌ 文件不存在，跳过导出" >> "$OUTPUT_FILE"
    fi
    echo -e "\n\n" >> "$OUTPUT_FILE"
}

# 写入文档标题
echo "=============================================" >> "$OUTPUT_FILE"
echo "  嵌入式Linux视觉采集终端 - 工程构建架构文件" >> "$OUTPUT_FILE"
echo "  仅包含：Makefile + 全局配置 + 工程架构" >> "$OUTPUT_FILE"
echo "  项目根目录：$PROJECT_ROOT" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 遍历手动配置的文件，执行导出 =====================
echo -e "\033[34mℹ️  开始导出工程架构，共 ${#EXPORT_FILES[@]} 个文件...\033[0m"
for file in "${EXPORT_FILES[@]}"; do
    export_file "$file"
done

# ===================== 【你要求的固定完成提示格式】 =====================
echo -e "\033[32m==================================================\033[0m"
echo -e "\033[32m✅ 工程架构导出完成！\033[0m"
echo -e "\033[32m📂 项目根目录：\033[0m"
echo -e "\033[32m$PROJECT_ROOT \033[0m"
echo -e "\033[32m📄 输出文件：\033[0m"
echo -e "\033[32m$OUTPUT_FILE \033[0m"
echo -e "\033[32m==================================================\033[0m"