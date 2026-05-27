#!/bin/sh
# VisionAI 看门狗监控脚本（IMX6UL 无systemd专用）

# ===================== 固定配置 =====================
APP_BIN="/root/run_on_board/vision_ai_app"
PROC_NAME="vision_ai_app"
WATCHDOG_LOG="/var/log/app_watchdog.log"
CHECK_INTERVAL=2
RETRY_DELAY=3
PID_FILE="/var/run/app_watchdog.pid"
# ====================================================

# 单例限制
if [ -f $PID_FILE ]; then
    PID=$(cat $PID_FILE)
    if ps | grep $PID | grep -v grep >/dev/null; then
        echo "看门狗已运行，退出"
        exit 1
    fi
fi
echo $$ > $PID_FILE
trap "rm -f $PID_FILE; exit 0" SIGINT SIGTERM

# 日志函数
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" >> $WATCHDOG_LOG
}

log "==================== 看门狗启动 ===================="
log "监控程序：$APP_BIN"

# 加载环境变量（修复正确路径）
. /root/run_on_board/auto/set_env.sh

# 主循环
cd /root/run_on_board
while true; do
    if ! ps | grep $PROC_NAME | grep -v grep >/dev/null; then
        log "⚠️ 程序已退出，尝试重启..."
        $APP_BIN
        log "✅ 程序重启完成"
        sleep $RETRY_DELAY
    fi
    sleep $CHECK_INTERVAL
done