#!/bin/sh
# 一键备份：日志 + 抓拍图片 + 核心文件 → /mnt/tmp
# 适用：i.MX6ULL 裁剪Buildroot系统

# 目标备份目录
TARGET_DIR="/mnt/tmp"

# 1. 清空并创建分类目录
rm -rf $TARGET_DIR
mkdir -p $TARGET_DIR/{log,face_capture,app,config} 2>/dev/null

echo "========================================"
echo "      开始备份所有文件到 /mnt/tmp"
echo "========================================"

# 2. 备份【全部日志】
echo "✅ 备份日志文件..."
cp -rf /mnt/sdcard/log/* $TARGET_DIR/log/ 2>/dev/null

# 3. 备份【全部抓拍图片】
echo "✅ 备份抓拍图片..."
cp -rf /mnt/sdcard/face_capture/* $TARGET_DIR/face_capture/ 2>/dev/null


echo "========================================"
echo "      备份完成！所有文件在：/mnt/tmp"
echo "========================================"
echo "查看日志：ls /mnt/tmp/log"
echo "查看图片：ls /mnt/tmp/face_capture"