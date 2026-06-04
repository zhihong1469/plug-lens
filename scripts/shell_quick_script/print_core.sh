#!/bin/bash
# 嵌入式Linux视觉项目 - 核心源码导出脚本
# ==============================================
# 【🔥 手动配置区 - 仅需填写相对路径】
PROJECT_NAME="嵌入式LinuxAI人脸抓拍实时推流项目"  # 项目名称（仅用于输出文件标题）
# 1. 脚本 相对于 项目根目录的路径 (VSCode复制相对路径)
SCRIPT_REL_PATH="scripts/shell_quick_script/print_core.sh"

# 2. 输出文件 相对于 项目根目录的路径
OUTPUT_REL_PATH="scripts/shell_quick_script/tmp/core_project_code.txt"

# 3. 需要导出的文件列表（支持 单个文件 / 通配符批量导出）
EXPORT_FILES=(
    # 应用入口 / 脚本文件（支持通配符）
    "drv/led_drv/*.c"
)
# ==============================================

# ===================== 核心：自动计算路径（零报错） =====================
# 1. 获取脚本所在绝对目录
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# 2. 自动计算项目根目录
PROJECT_ROOT=$(cd "$SCRIPT_DIR/../../" && pwd)

# 3. 最终输出文件绝对路径
OUTPUT_FILE="$PROJECT_ROOT/$OUTPUT_REL_PATH"
OUTPUT_DIR=$(dirname "$OUTPUT_FILE")
# ======================================================================

# ===================== 自动创建输出目录 + 明确提示 =====================
if [ -d "$OUTPUT_DIR" ]; then
    echo -e "\033[34mℹ️  输出目录已存在：$OUTPUT_DIR\033[0m"
else
    mkdir -p "$OUTPUT_DIR"
    echo -e "\033[32m✅ 成功创建输出目录：$OUTPUT_DIR\033[0m"
fi

# 清空旧文件
> "$OUTPUT_FILE"

# ===================== 安全导出函数 =====================
export_file() {
    local ABS_FILE="$1"
    # 提取相对路径（用于展示）
    local REL_FILE="${ABS_FILE#$PROJECT_ROOT/}"
    
    # 写入文件标题
    echo "=====================================" >> "$OUTPUT_FILE"
    echo "📄 导出文件：$REL_FILE" >> "$OUTPUT_FILE"
    echo "=====================================" >> "$OUTPUT_FILE"

    # 检查并写入内容
    if [ -f "$ABS_FILE" ]; then
        cat "$ABS_FILE" >> "$OUTPUT_FILE"
    else
        echo -e "\033[31m⚠️  缺失文件：$REL_FILE\033[0m"
        echo "❌ 文件不存在，跳过导出" >> "$OUTPUT_FILE"
    fi
    echo -e "\n\n" >> "$OUTPUT_FILE"
}

# ===================== 执行导出（支持通配符核心逻辑） =====================
# 写入文档标题
echo "============ $PROJECT_NAME - 核心接口源码 ============" >> "$OUTPUT_FILE"
echo "✅ 导出规范：优先导出头文件/脚本文件" >> "$OUTPUT_FILE"
echo "✅ 项目根目录：$PROJECT_ROOT" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo -e "\033[34mℹ️  开始批量导出文件（支持通配符）...\033[0m"

# 🔥 核心升级：遍历通配符，批量匹配文件
for pattern in "${EXPORT_FILES[@]}"; do
    full_pattern="$PROJECT_ROOT/$pattern"
    # 展开通配符，遍历所有匹配的文件
    for file in $full_pattern; do
        export_file "$file"
    done
done

# ===================== 固定完成提示 =====================
echo -e "\033[32m==================================================\033[0m"
echo -e "\033[32m✅ 源码导出完成！\033[0m"
echo -e "\033[32m📂 项目根目录：\033[0m"
echo -e "\033[32m$PROJECT_ROOT \033[0m"
echo -e "\033[32m📄 输出文件：\033[0m"
echo -e "\033[32m$OUTPUT_FILE \033[0m"
echo -e "\033[32m==================================================\033[0m"