#!/bin/sh
APP_NAME="vision_ai_app"
APP_PATH="/root/run_on_board/$APP_NAME"
PID_FILE="/var/run/app_watchdog.pid"
LOG="/mnt/sdcard/log/watchdog.log"

# 单例
[ -f $PID_FILE ] && exit 0
echo $$ > $PID_FILE

# 退出清理
trap "rm -f $PID_FILE; exit" SIGINT SIGTERM

# 切目录
cd /root/run_on_board || exit 1

# ======================================
# 看门狗内部强制加载环境（最关键！）
# 守护进程会继承这个环境变量
# ======================================
. ./auto/set_env.sh

echo "[$(date)] 看门狗启动，监控：$APP_NAME" >> $LOG

# 循环监控
while true; do
    if ! ps | grep -v grep | grep -q "$APP_NAME"; then
        echo "[$(date)] 进程崩溃，重启中..." >> $LOG
        sleep 2
        # 启动业务（环境变量已自带，守护进程直接继承）
        nohup "$APP_PATH" >> /mnt/sdcard/log/app.log 2>&1 &
    fi
    sleep 3
done