#!/bin/bash

# 【固定】目标NFS目录
DEST_DIR="$HOME/nfs/run_on_board/drv"
SRC_DIR="drv/led_drv/"

# 自动创建目标目录
mkdir -p "$DEST_DIR"

echo "============================================"
echo "源目录：$SRC_DIR"
echo "目标目录：$DEST_DIR"
echo "============================================"

# 复制核心文件
echo -e "\n正在复制驱动和测试程序..."
cp -vf "$SRC_DIR"*.ko        "$DEST_DIR"
cp -vf "$SRC_DIR"ledtest     "$DEST_DIR"
