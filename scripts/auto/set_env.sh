#!/bin/sh
# 一键配置VisionAI程序运行环境变量
chmod +x /root/run_on_board/auto/*.sh
# 注意：无需手动source！脚本内部自动调用
export APP_HOME="/root/run_on_board"
export LD_LIBRARY_PATH=\
$APP_HOME/openh264:\
$APP_HOME/libjpeg:\
$APP_HOME/mnn:\
$APP_HOME/libyuv:\
$LD_LIBRARY_PATH
# 仅调试打印，量产可注释
echo "✅ 环境变量已加载：$LD_LIBRARY_PATH"
date -s "2026-05-29 00:00:00"
insmod /root/run_on_board/drv/leddrv.ko
insmod /root/run_on_board/drv/chip_demo_gpio.ko 
insmod /root/run_on_board/drv/board_A_led.ko 
./run_on_board/auto/app_start.sh

# 自己写到环境变量里更方便