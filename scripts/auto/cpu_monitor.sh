#!/bin/sh
LOG_FILE="/mnt/test/log/cpu_status.log"
MONITOR_INTERVAL=10

mkdir -p /mnt/test/log 2>/dev/null

echo "[$(date '+%Y-%m-%d %H:%M:%S')] cpu_monitor start" >> "$LOG_FILE"

while true; do
    echo "========================================" >> "$LOG_FILE"
    echo "now time:$(date '+%Y-%m-%d %H:%M:%S')" >> "$LOG_FILE"
    top -n 1 | head -n 4 >> "$LOG_FILE"
    top -n 1 | grep ./vision_ai_app >> "$LOG_FILE"
    sleep $MONITOR_INTERVAL
done