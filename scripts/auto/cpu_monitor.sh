#!/bin/sh
# 功能：裁剪系统适配版资源监控，低频率、低占用
# 适配：BusyBox top（无-b/-n参数）、精简ps命令

LOG_FILE="/mnt/sdcard/log/cpu_status.log"
MONITOR_INTERVAL=10  # 采样间隔(秒)
mkdir -p /mnt/sdcard/log 2>/dev/null
echo "[$(date '+%Y-%m-%d %H:%M:%S')] cpu_monitor start" >> "$LOG_FILE"

while true; do
    echo "========================================" >> "$LOG_FILE"
    echo "now time:$(date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE"
    top -n 1 | head -n 4 >> "$LOG_FILE"
    top -n 1 | grep ./vision_ai_app >> "$LOG_FILE"
    sleep $MONITOR_INTERVAL
done