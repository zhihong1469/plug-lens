#!/bin/bash
# 嵌入式Linux项目目录树导出脚本
# ==============================================
# 【🔥 手动配置区 - 仅需填写相对路径】
PROJECT_NAME="嵌入式LinuxAI人脸抓拍实时推流项目"  # 项目名称（仅用于输出文件标题）
# 1. 脚本 相对于 项目根目录的路径 (VSCode复制相对路径)
SCRIPT_REL_PATH="scripts/shell_quick_script/print_tree.sh"

# 2. 输出文件 相对于 项目根目录的路径
OUTPUT_REL_PATH="scripts/shell_quick_script/tmp/catalog_tree.txt"

# 3. 【新增】要扫描的目标目录 相对于 项目根目录的路径（留空则扫描整个项目）
#    示例：.trae    ← 扫描 .trae 目录
#    示例：src      ← 扫描 src 目录
#    示例：(留空)   ← 扫描整个项目根目录
TARGET_DIR=".trae"
# ==============================================

# ===================== 核心：自动计算路径（零报错） =====================
# 1. 获取脚本所在绝对目录
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# 2. 自动计算项目根目录（固定逻辑，无需手动算层级）
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

# 容错检查：根目录是否有效
if [ ! -d "$PROJECT_ROOT" ]; then
    echo -e "\033[31m❌ 错误：项目根目录定位失败！\033[0m"
    exit 1
fi

# ===================== 确定扫描目标目录 =====================
if [ -n "$TARGET_DIR" ]; then
    SCAN_TARGET="$PROJECT_ROOT/$TARGET_DIR"
    SCAN_DISPLAY="$PROJECT_ROOT/$TARGET_DIR (相对路径: $TARGET_DIR)"
    
    if [ ! -d "$SCAN_TARGET" ]; then
        echo -e "\033[31m❌ 错误：指定的扫描目录不存在：$SCAN_TARGET\033[0m"
        exit 1
    fi
else
    SCAN_TARGET="$PROJECT_ROOT"
    SCAN_DISPLAY="$PROJECT_ROOT (整个项目根目录)"
fi

# 清空旧文件
> "$OUTPUT_FILE"

# ===================== 导出目录树 =====================
# 写入标题
echo "============ $PROJECT_NAME - 实际目录结构 ============" >> "$OUTPUT_FILE"
echo "✅ 项目根目录：$PROJECT_ROOT" >> "$OUTPUT_FILE"
echo "✅ 扫描目录：$SCAN_DISPLAY" >> "$OUTPUT_FILE"
echo "✅ 包含隐藏文件：是" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# 执行tree命令
# -a: 显示所有文件（包括隐藏文件）
# -I: 排除不需要的目录和文件类型
tree -a -I "build|scripts|.git|*.o|*.so|*.sh|*.a|*.log|*.md|*.txt|*.png|*.d|*.tmp|out|tags|third_lib" "$SCAN_TARGET" >> "$OUTPUT_FILE"

# ===================== 完成提示（统一风格） =====================
echo -e "\033[32m==================================================\033[0m"
echo -e "\033[32m✅ 目录树导出完成！\033[0m"
echo -e "\033[32m📂 项目根目录：\033[0m"
echo -e "\033[32m$PROJECT_ROOT\033[0m"
echo -e "\033[32m📂 扫描目录：\033[0m"
echo -e "\033[32m$SCAN_DISPLAY\033[0m"
echo -e "\033[32m📄 输出文件：\033[0m"
echo -e "\033[32m$OUTPUT_FILE\033[0m"
echo -e "\033[32m==================================================\033[0m"