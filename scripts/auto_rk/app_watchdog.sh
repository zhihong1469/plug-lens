#!/bin/sh
# 配置参数 - RK3562 版本
APP_NAME="vision_ai_app"
APP_PATH="/mnt/nfs/run_on_board_rk3562/$APP_NAME"
PID_FILE="/var/run/app_watchdog.pid"
LOG_FILE="/mnt/nfs/test/log/watchdog.log"
CHECK_INTERVAL=3
RESTART_DELAY=3

# 单例检查
if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE" 2>/dev/null)
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
        echo "[$(date)] 看门狗已运行，PID: $OLD_PID" >> "$LOG_FILE"
        exit 0
    fi
    rm -f "$PID_FILE"
fi
echo $$ > "$PID_FILE"

# 退出清理
trap 'rm -f "$PID_FILE"; exit' INT TERM

cd /mnt/nfs/run_on_board_rk3562 || exit 1
. ./auto_rk/set_env.sh

echo "[$(date)] 看门狗启动，监控进程：$APP_NAME (RK3562)" >> "$LOG_FILE"

while true; do
    if ! ps | grep -v grep | grep -q "$APP_NAME"; then
        echo "[$(date)] 进程$APP_NAME已退出，准备重启" >> "$LOG_FILE"
        sleep $RESTART_DELAY
        nohup "$APP_PATH" >> /mnt/nfs/test/log/app.log 2>&1 &
        echo "[$(date)] 已重启$APP_NAME" >> "$LOG_FILE"
    fi
    sleep $CHECK_INTERVAL
done