#!/bin/bash
# 嵌入式Linux项目 - 开发板文件快速部署脚本（复制到NFS/目标目录）
# ==============================================
# 【🔥 手动配置区 - 自由修改】
# 1. 脚本相对项目根目录的路径
SCRIPT_REL_PATH="scripts/shell_quick_script/copy_to.sh"

# 2. 【多个源目录】相对根目录的路径（数组，可增删）
SOURCE_DIRS=(
    "drv/led_drv/"
    # 可添加更多："output/"
)

# 3. 【要复制的文件规则】（支持通配符/固定文件名）
COPY_FILES=(
    "*.ko"
    "ledtest"
    # 可添加更多："*.so" "vision_ai_app"
)

# 4. 目标部署目录（NFS/开发板目录）
DEST_DIR="$HOME/nfs/run_on_board/drv"
# ==============================================

# ===================== 自动路径计算 =====================
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/../../" && pwd)
# ========================================================

# 自动创建目标目录
if [ -d "$DEST_DIR" ]; then
    echo -e "\033[34mℹ️  目标目录已存在：$DEST_DIR\033[0m"
else
    mkdir -p "$DEST_DIR"
    echo -e "\033[32m✅ 成功创建目标目录：$DEST_DIR\033[0m"
fi

echo -e "\n\033[34mℹ️  开始批量复制文件到开发板目录...\033[0m"

# 核心：循环遍历 源目录 + 文件规则，批量复制
for src_dir in "${SOURCE_DIRS[@]}"; do
    abs_src_dir="$PROJECT_ROOT/$src_dir"
    [ ! -d "$abs_src_dir" ] && echo -e "\033[31m⚠️  源目录不存在：$abs_src_dir\033[0m" && continue

    # 🔥 修改点：统一显示【绝对路径】，无歧义
    echo -e "\n📂 处理源目录："
    echo -e "\n$abs_src_dir"
    for pattern in "${COPY_FILES[@]}"; do
        cp -vf "$abs_src_dir$pattern" "$DEST_DIR" 2>/dev/null \
        || echo -e "\033[31m  └─ 无匹配文件：$pattern\033[0m"
    done
done

# ===================== 【固定完成提示格式】 =====================
echo -e "\033[32m==================================================\033[0m"
echo -e "\033[32m✅ 文件复制完成！\033[0m"
echo -e "\033[32m📂 项目根目录：\033[0m"
echo -e "\033[32m$PROJECT_ROOT \033[0m"
echo -e "\033[32m📦 目标目录：\033[0m"
echo -e "\033[32m$DEST_DIR \033[0m"
echo -e "\033[32m==================================================\033[0m"