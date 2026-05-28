#!/bin/sh
# 仅负责设置环境变量，被所有脚本自动调用
# 禁止添加任何启动、驱动、业务逻辑！
export APP_HOME="/root/run_on_board"
export LD_LIBRARY_PATH=$APP_HOME/openh264:$APP_HOME/libjpeg:$APP_HOME/mnn:$APP_HOME/libyuv