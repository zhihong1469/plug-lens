#!/bin/sh
# 配置参数
APP_NAME="vision_ai_app"
APP_PATH="/root/run_on_board/$APP_NAME"
PID_FILE="/var/run/app_watchdog.pid"
LOG_FILE="/mnt/sdcard/log/watchdog.log"
CHECK_INTERVAL=3    # 检测间隔(秒)
RESTART_DELAY=3     # 重启延时(秒)，防雪崩

# 【增强】单例检查+PID残留清理
if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE" 2>/dev/null)
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
        echo "[$(date)] 看门狗已运行，PID: $OLD_PID" >> "$LOG_FILE"
        exit 0
    fi
    rm -f "$PID_FILE"
fi
echo $$ > "$PID_FILE"

# 退出自动清理PID文件
trap 'rm -f "$PID_FILE"; exit' SIGINT SIGTERM

# 进入工作目录
cd /root/run_on_board || exit 1

# 加载环境变量（守护进程自动继承）
. ./auto/set_env.sh

echo "[$(date)] 看门狗启动，监控进程：$APP_NAME" >> "$LOG_FILE"

# 循环监控
while true; do
    if ! ps | grep -v grep | grep -q "$APP_NAME"; then
        echo "[$(date)] 进程$APP_NAME已退出，准备重启" >> "$LOG_FILE"
        sleep $RESTART_DELAY
        # 启动业务程序
        nohup "$APP_PATH" >> /mnt/sdcard/log/app.log 2>&1 &
        echo "[$(date)] 已重启$APP_NAME" >> "$LOG_FILE"
    fi
    sleep $CHECK_INTERVAL
done