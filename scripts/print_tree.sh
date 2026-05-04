#!/bin/bash
# 嵌入式Linux视觉项目 - 项目目录树导出
# 脚本位置：scripts/print_tree.sh
# 输出位置：scripts/catalog_tree.txt
# 用法：chmod +x scripts/print_tree.sh && ./scripts/print_tree.sh

# 自动定位路径
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
OUTPUT_FILE="$SCRIPT_DIR/catalog_tree.txt"

# 清空旧文件
> "$OUTPUT_FILE"

# 标题
echo "============ 嵌入式Linux视觉采集终端 - 实际目录结构 ============" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# 导出目录树（过滤编译/构建/临时文件）
tree -I "build|scripts|.git|*.o|*.so|*.sh|*.a|*.log|*.md|*.txt|*.png|*.d|*.tmp|out|tags" "$PROJECT_ROOT" >> "$OUTPUT_FILE"

# 完成提示
echo -e "\033[32m ✅ 项目目录树已导出：$OUTPUT_FILE \033[0m"
echo -e "\033[32m ✅ 直接复制发给AI即可！ \033[0m"