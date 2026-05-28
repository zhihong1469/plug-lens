#!/bin/sh
# 日志轮转脚本，防止SD卡溢出
# 建议每天执行一次，可加入crontab

# 日志路径（和实际写入路径完全一致）
APP_LOG="/mnt/sdcard/log/app.log"
WDOG_LOG="/mnt/sdcard/log/watchdog.log"
CPU_LOG="/mnt/sdcard/log/cpu_status.log"

# 大小限制：20MB/15MB
MAX_APP=$((20*1024*1024))
MAX_WDOG=$((15*1024*1024))
MAX_CPU=$((15*1024*1024))

# 保留最近5份历史日志
KEEP=5

rotate() {
    local file=$1
    local max_size=$2
    local keep_num=$3
    
    # 文件不存在或小于限制，直接返回
    if [ ! -f "$file" ]; then
        return
    fi
    local size=$(du -b "$file" | awk '{print $1}' 2>/dev/null || echo 0)
    if [ "$size" -lt "$max_size" ]; then
        return
    fi

    # 轮转历史日志
    for i in $(seq $keep_num -1 2); do
        local prev=$((i-1))
        mv -f "${file}.${prev}" "${file}.${i}" 2>/dev/null
    done
    mv -f "$file" "${file}.1" 2>/dev/null
    > "$file"  # 清空当前日志
}

# 执行轮转
rotate "$APP_LOG" "$MAX_APP" "$KEEP"
rotate "$WDOG_LOG" "$MAX_WDOG" "$KEEP"
rotate "$CPU_LOG" "$MAX_CPU" "$KEEP"

echo "✅ 日志轮转完成，保留最近$KEEP份日志"