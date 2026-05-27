#!/bin/sh
# 一键配置VisionAI程序运行环境变量（适配IMX6UL）
export APP_HOME=/root/run_on_board

# 核心库路径（严格匹配你的目录结构）
export LD_LIBRARY_PATH=\
$APP_HOME/openh264:\
$APP_HOME/libjpeg:\
$APP_HOME/mnn:\
$APP_HOME/libyuv:\
$LD_LIBRARY_PATH

# 打印生效信息
echo "✅ 环境变量配置完成"
echo "程序目录：$APP_HOME"
echo "库路径：$LD_LIBRARY_PATH"