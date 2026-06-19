pc端执行以下命令
cp output/vision_ai_app ~/nfs/run_on_board_rk3562/
板端执行以下命令
export APP_HOME="/mnt/nfs/run_on_board_rk3562"
export LD_LIBRARY_PATH=$APP_HOME/openh264:$APP_HOME/libjpeg:$APP_HOME/mnn:$APP_HOME/libyuv

./vision_ai_app
killall -9 vision_ai_app