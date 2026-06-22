#!/bin/sh
# RK3562 CPU 监控脚本
# 用于后台监控 CPU 使用率

LOG_FILE="/mnt/nfs/test/log/cpu_status.log"
MONITOR_INTERVAL=10

mkdir -p /mnt/nfs/test/log 2>/dev/null

echo "[$(date '+%Y-%m-%d %H:%M:%S')] cpu_monitor start" >> "$LOG_FILE"

while true; do
    echo "========================================" >> "$LOG_FILE"
    echo "now time:$(date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE"
    # 使用 -b 选项启用批处理模式，避免 tty 错误
    top -b -n 1 | head -n 10 >> "$LOG_FILE"
    top -b -n 1 | grep vision_ai_app >> "$LOG_FILE" 2>/dev/null
    sleep $MONITOR_INTERVAL
done