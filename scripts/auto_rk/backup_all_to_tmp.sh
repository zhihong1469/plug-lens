#!/bin/sh
# 一键备份：日志 + 抓拍图片 → /mnt/tmp
TARGET_DIR="/mnt/tmp"

rm -rf $TARGET_DIR
mkdir -p $TARGET_DIR/{log,face_capture} 2>/dev/null

echo "========================================"
echo "      开始备份所有文件到 /mnt/tmp"
echo "========================================"

echo "✅ 备份日志文件..."
cp -rf /mnt/nfs/test/log/* $TARGET_DIR/log/ 2>/dev/null

echo "✅ 备份抓拍图片..."
cp -rf /mnt/nfs/test/face_capture/* $TARGET_DIR/face_capture/ 2>/dev/null

echo "========================================"
echo "      备份完成！所有文件在：/mnt/tmp"
echo "========================================"