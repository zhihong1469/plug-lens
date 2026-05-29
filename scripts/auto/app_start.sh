#!/bin/sh
# 【强制】绝对路径进入工作目录，永不报错
cd /root/run_on_board || exit 1

echo "========================================"
echo "          启动 VisionAI 系统"
echo "========================================"

# 加载环境变量
. ./auto/set_env.sh
echo "✅ 环境变量加载完成"

# 兜底创建日志/抓拍目录（网络路径）
mkdir -p /mnt/test/log 2>/dev/null
mkdir -p /mnt/test/face_capture 2>/dev/null

# 加载驱动（重复加载自动屏蔽报错）
insmod ./drv/leddrv.ko 2>/dev/null
insmod ./drv/chip_demo_gpio.ko 2>/dev/null
insmod ./drv/board_A_led.ko 2>/dev/null
echo "✅ LED驱动加载完成"

# 启动看门狗
nohup ./auto/app_watchdog.sh >> /mnt/test/log/watchdog.log 2>&1 &
echo "✅ 软件看门狗已启动"

# 启动CPU监控
nohup ./auto/cpu_monitor.sh >> /mnt/test/log/cpu_status.log 2>&1 &

echo "========================================"
echo "          系统启动完成"
echo " 日志路径：/mnt/test/log/"
echo " 查看日志：tail -f /mnt/test/log/app.log"
echo "========================================"