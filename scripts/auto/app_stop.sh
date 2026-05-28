#!/bin/sh
# 功能：优雅停止全套服务：看门狗 → 业务程序 → 兜底杀进程
# 顺序：先停监控，再停业务，避免僵尸进程

# 1. 停止看门狗
WATCHDOG_PID_FILE="/var/run/app_watchdog.pid"
if [ -f "$WATCHDOG_PID_FILE" ]; then
    WATCHDOG_PID=$(cat "$WATCHDOG_PID_FILE")
    kill "$WATCHDOG_PID" 2>/dev/null
    rm -f "$WATCHDOG_PID_FILE"
    echo "===== 已停止软件看门狗 ====="
fi

# 2. 停止业务程序
APP_NAME="vision_ai_app"
killall "$APP_NAME" 2>/dev/null
sleep 1

# 3. 兜底强制杀死（防止僵死）
killall -9 "$APP_NAME" 2>/dev/null
killall app_watchdog.sh 2>/dev/null
killall cpu_monitor.sh 2>/dev/null

echo "===== 所有业务服务已停止 ====="