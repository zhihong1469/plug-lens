#!/bin/sh
APP_LOG="/mnt/test/log/app.log"
WDOG_LOG="/mnt/test/log/watchdog.log"
CPU_LOG="/mnt/test/log/cpu_status.log"

MAX_APP=$((20*1024*1024))
MAX_WDOG=$((15*1024*1024))
MAX_CPU=$((15*1024*1024))
KEEP=5

rotate() {
    local file=$1
    local max_size=$2
    local keep_num=$3
    
    if [ ! -f "$file" ]; then return; fi
    local size=$(du -b "$file" | awk '{print $1}' 2>/dev/null || echo 0)
    if [ "$size" -lt "$max_size" ]; then return; fi

    for i in $(seq $keep_num -1 2); do
        local prev=$((i-1))
        mv -f "${file}.${prev}" "${file}.${i}" 2>/dev/null
    done
    mv -f "$file" "${file}.1" 2>/dev/null
    > "$file"
}

rotate "$APP_LOG" "$MAX_APP" "$KEEP"
rotate "$WDOG_LOG" "$MAX_WDOG" "$KEEP"
rotate "$CPU_LOG" "$MAX_CPU" "$KEEP"

echo "✅ 日志轮转完成"