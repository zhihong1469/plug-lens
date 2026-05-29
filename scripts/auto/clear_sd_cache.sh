#!/bin/sh
# 清空网络目录缓存 /mnt/test
LOG_DIR="/mnt/test/log"
CAPTURE_DIR="/mnt/test/face_capture"

echo "========================================"
echo "       【安全】清空运行缓存"
echo "========================================"
read -p "确认清空？(y/n): " confirm
[ "$confirm" != "y" ] && exit 0

# 只删文件，不删目录
find $LOG_DIR -type f -delete 2>/dev/null
find $CAPTURE_DIR -type f -delete 2>/dev/null

echo "✅ 清空完成！"