#!/bin/sh
# 日志循环轮转脚本（适配SD卡存储）
# 路径：/root/run_on_board/log_rotate.sh

# 日志路径
APP_LOG="/mnt/sdcard/log/app.log"
WDOG_LOG="/var/log/app_watchdog.log"
CPU_LOG="/mnt/sdcard/log/cpu_status.log"

# 大小限制
MAX_APP=$((20*1024*1024))
MAX_WDOG=$((15*1024*1024))
MAX_CPU=$((15*1024*1024))

# 保留份数
KEEP=5

rotate() {
    file=$1
    max=$2
    keep=$3
    size=$(du -b $file | awk '{print $1}' 2>/dev/null || echo 0)
    
    if [ $size -lt $max ]; then
        return
    fi

    # 循环重命名历史日志
    for i in $(seq $keep -1 2); do
        prev=$((i-1))
        mv -f ${file}.${prev} ${file}.${i} 2>/dev/null
    done
    mv -f $file ${file}.1 2>/dev/null
    > $file
}

# 执行轮转
rotate $APP_LOG $MAX_APP $KEEP
rotate $WDOG_LOG $MAX_WDOG $KEEP
rotate $CPU_LOG $MAX_CPU $KEEP