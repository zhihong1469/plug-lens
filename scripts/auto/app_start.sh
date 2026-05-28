#!/bin/sh
# 【强制】绝对路径进入工作目录，永不报错
cd /root/run_on_board || exit 1

echo "========================================"
echo "          启动 VisionAI 系统"
echo "========================================"

# 加载环境变量
. ./auto/set_env.sh
echo "✅ 环境变量加载完成"

# 兜底创建日志/抓拍目录
mkdir -p /mnt/sdcard/log 2>/dev/null
mkdir -p /mnt/sdcard/face_capture 2>/dev/null

# 加载驱动（双重保险，重复加载自动屏蔽报错）
insmod ./drv/leddrv.ko 2>/dev/null
insmod ./drv/chip_demo_gpio.ko 2>/dev/null
insmod ./drv/board_A_led.ko 2>/dev/null
echo "✅ LED驱动加载完成"

# 启动看门狗（后台运行，日志写入文件）
nohup ./auto/app_watchdog.sh >> /mnt/sdcard/log/watchdog.log 2>&1 &
echo "✅ 软件看门狗已启动"

# 可选：启动CPU监控（调试用，量产可注释）
nohup ./auto/cpu_monitor.sh >> /mnt/sdcard/log/cpu_status.log 2>&1 &
# echo "✅ CPU监控已启动"

echo "========================================"
echo "          系统启动完成"
echo " 日志路径：/mnt/sdcard/log/"
echo " 查看日志：tail -f /mnt/sdcard/log/app.log"
echo "========================================"