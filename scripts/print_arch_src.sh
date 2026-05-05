#!/bin/bash
# 嵌入式Linux视频AI项目 - 仅导出【工程架构/构建系统】文件
# 脚本位置：scripts/print_arch_src.sh
# 输出位置：项目根目录/project_arch_code.txt
# 用法：chmod +x scripts/print_arch_src.sh && ./scripts/print_arch_src.sh

# ===================== 核心路径配置（自动定位根目录） =====================
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
# 输出到项目根目录，更方便查找
OUTPUT_FILE="$PROJECT_ROOT/scripts/project_arch_code.txt"

# 清空旧文件
> "$OUTPUT_FILE"

# ===================== 开始写入 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "  嵌入式Linux视觉采集终端 - 工程构建架构文件" >> "$OUTPUT_FILE"
echo "  仅包含：Makefile + 全局配置 + 工程架构" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 1. 顶层构建系统（核心架构） =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【顶层构建】Makefile (项目总编译入口)" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/Makefile" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【顶层构建】Makefile.build (通用编译规则)" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/Makefile.build" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 2. 公共层架构文件 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/Makefile" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/Makefile" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/configs/main.h (主配置头)" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/configs/main.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

echo "=============================================" >> "$OUTPUT_FILE"
echo "【公共层】common/configs/vision_ai_config.h (全局AI配置)" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/common/configs/vision_ai_config.h" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 3. 源码层架构文件 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【源码层】src/Makefile" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/src/Makefile" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 4. 插件层架构文件 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "【插件层】plugins/Makefile" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"
cat "$PROJECT_ROOT/plugins/Makefile" >> "$OUTPUT_FILE"
echo -e "\n\n" >> "$OUTPUT_FILE"

# ===================== 完成提示 =====================
echo "=============================================" >> "$OUTPUT_FILE"
echo "  工程架构文件导出完成！" >> "$OUTPUT_FILE"
echo "=============================================" >> "$OUTPUT_FILE"

# 终端彩色提示
echo -e "\033[32m ============================================== \033[0m"
echo -e "\033[32m ✅ 脚本路径：$SCRIPT_DIR/print_arch_src.sh \033[0m"
echo -e "\033[32m ✅ 架构文件已导出到：$OUTPUT_FILE \033[0m"
echo -e "\033[32m ✅ 纯架构文件，AI分析更精准！ \033[0m"
echo -e "\033[32m ============================================== \033[0m"