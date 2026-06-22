#!/bin/sh
export APP_HOME="/mnt/nfs/run_on_board_rk3562"
export LD_LIBRARY_PATH=$APP_HOME/libjpeg:$APP_HOME/rk3562/lib:$APP_HOME/drv:$APP_HOME/rknn:$APP_HOME/rkmpp
