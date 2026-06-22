#!/bin/sh
# 【强制】绝对路径进入工作目录，永不报错
cd /mnt/nfs/run_on_board_rk3562 || exit 1

echo "========================================"
echo "          启动 VisionAI 系统 (RK3562)"
echo "========================================"

# 加载环境变量
. ./auto_rk/set_env.sh
echo "✅ 环境变量加载完成"

# 兜底创建日志/抓拍目录（网络路径）
mkdir -p /mnt/nfs/test/log 2>/dev/null
mkdir -p /mnt/nfs/test/face_capture 2>/dev/null

# RK3562 无需加载 LED 驱动（使用系统 GPIO）
echo "✅ RK3562 GPIO 已就绪"

# 启动看门狗
nohup ./auto_rk/app_watchdog.sh >> /mnt/nfs/test/log/watchdog.log 2>&1 &
echo "✅ 软件看门狗已启动"

# 启动CPU监控
nohup ./auto_rk/cpu_monitor.sh >> /mnt/nfs/test/log/cpu_status.log 2>&1 &

# 启动主程序
./vision_ai_app &

echo "========================================"
echo "          系统启动完成 (RK3562)"
echo " 日志路径：/mnt/nfs/test/log/"
echo " 查看日志：tail -f /mnt/nfs/test/log/app.log"
echo "========================================"