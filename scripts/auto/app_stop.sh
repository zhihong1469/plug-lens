#!/bin/sh
echo "========================================"
echo "          停止 VisionAI 系统"
echo "========================================"

# 1. 停止看门狗（优先停止，防止自动重启）
WATCHDOG_PID_FILE="/var/run/app_watchdog.pid"
if [ -f "$WATCHDOG_PID_FILE" ]; then
    WATCHDOG_PID=$(cat "$WATCHDOG_PID_FILE" 2>/dev/null)
    kill "$WATCHDOG_PID" 2>/dev/null
    rm -f "$WATCHDOG_PID_FILE"
    echo "✅ 已停止软件看门狗"
fi

# 2. 停止业务程序
APP_NAME="vision_ai_app"
killall "$APP_NAME" 2>/dev/null
sleep 1

# 3. 兜底强制杀死所有残留进程
killall -9 "$APP_NAME" 2>/dev/null
killall app_watchdog.sh 2>/dev/null
killall cpu_monitor.sh 2>/dev/null
killall app_start.sh 2>/dev/null

echo "✅ 所有业务服务已停止"
echo "========================================"