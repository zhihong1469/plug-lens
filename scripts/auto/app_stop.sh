#!/bin/sh
# 一键优雅停止（标准顺序：先停程序 → 再停看门狗）
echo "🛑 停止 VisionAI 系统..."

# 1. 停止看门狗
kill $(pidof app_watchdog.sh) 2>/dev/null
sleep 1

# 2. 停止业务程序
kill $(pidof vision_ai_app) 2>/dev/null
sleep 2

# 强制兜底
kill -9 $(pidof vision_ai_app) 2>/dev/null
kill -9 $(pidof app_watchdog.sh) 2>/dev/null

echo "✅ 已全部停止"