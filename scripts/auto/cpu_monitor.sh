#!/bin/sh
# 【工业版】系统状态监控 - 仅抓取TOP前10行（适配IMX6UL BusyBox）
LOG_FILE="/mnt/sdcard/log/cpu_status.log"
TIME=$(date "+%Y-%m-%d %H:%M:%S")

# 写入时间戳
echo "=============================================" >> $LOG_FILE
echo "nowadays: $TIME" >> $LOG_FILE
top -n 1 | head -n 4 >> $LOG_FILE
top -n 1 | grep ./vision_ai_app >> $LOG_FILE
echo "" >> $LOG_FILE