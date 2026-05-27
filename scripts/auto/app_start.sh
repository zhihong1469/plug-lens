#!/bin/sh
# 一键启动整套系统（看门狗+程序+环境）
echo "🚀 启动 VisionAI 系统..."

# 1. 创建必要目录
mkdir -p /mnt/sdcard/log
mkdir -p /mnt/sdcard/face_capture

# 2. 加载环境变量
. /root/run_on_board/set_env.sh

# 3. 启动看门狗（后台）
/root/run_on_board/app_watchdog.sh &

echo "✅ 启动完成，看门狗已运行"
echo "👉 查看日志：tail -f /mnt/sdcard/log/app.log"