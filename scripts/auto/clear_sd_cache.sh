#!/bin/sh
# 【防自杀终极版】一键清空SD卡缓存
# 三重防护：绝对不会删除系统文件！

SD_MOUNT="/mnt/sdcard"
LOG_DIR="$SD_MOUNT/log"
CAPTURE_DIR="$SD_MOUNT/face_capture"

# 防护1：检查SD卡是否挂载，未挂载直接退出
if ! mount | grep -q "$SD_MOUNT"; then
    echo "❌ 致命保护：SD卡未挂载！禁止删除！"
    exit 1
fi

# 防护2：检查目录是否存在
if [ ! -d "$LOG_DIR" ] || [ ! -d "$CAPTURE_DIR" ]; then
    echo "❌ 错误：缓存目录不存在！"
    exit 1
fi

echo "========================================"
echo "       【安全】清空SD卡缓存"
echo "========================================"
read -p "确认清空？(y/n): " confirm
[ "$confirm" != "y" ] && exit 0

# 防护3：只删除文件，不删目录
find $LOG_DIR -type f -delete 2>/dev/null
find $CAPTURE_DIR -type f -delete 2>/dev/null

echo "✅ 清空完成！系统绝对安全！"