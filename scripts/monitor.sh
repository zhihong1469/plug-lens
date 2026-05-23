#!/bin/sh
# SPDX-License-Identifier: MIT
# ==============================================================================
# 嵌入式AI视觉终端 - 进程监控&自启脚本（产品级）
# 功能：开机初始化、环境配置、SD卡挂载、程序崩溃自动重启
# 适用平台：100ask IMX6ULL / BusyBox Linux
# ==============================================================================

# ========================== 【可配置核心参数】集中管理 ==========================
# 应用名称
APP_NAME="vision_ai_app"
# 【产品化标准路径】替代原调试/mnt目录（嵌入式Linux标准应用路径）
APP_BASE_DIR="/usr/local/vision_ai"
# 应用可执行文件路径
APP_PATH="${APP_BASE_DIR}/${APP_NAME}"
# SD卡挂载路径（与代码sd_storage.h严格对齐）
SD_CARD_MOUNT_PATH="/mnt/sdcard"
# 人脸抓拍存储目录
FACE_CAPTURE_PATH="${SD_CARD_MOUNT_PATH}/face_capture"
# NFS调试配置（产品模式可注释）
NFS_SERVER_IP="192.168.5.10"
NFS_SERVER_PATH="/home/luo/nfs"
NFS_MOUNT_PATH="/mnt"
# 时间同步默认值
DEFAULT_DATETIME="2026-05-22 12:00:00"

# ========================== 【系统初始化】 ==========================
echo "============================================================="
echo "[$(date)] 启动 Vision AI 系统初始化..."
echo "============================================================="

# 1. 同步系统时间
echo "[$(date)] 同步系统时间: ${DEFAULT_DATETIME}"
date -s "${DEFAULT_DATETIME}"

# 2. 【调试专用】挂载NFS（产品模式请注释本段）
# echo "[$(date)] 挂载NFS调试目录"
# mount -t nfs -o nolock,port=2050 ${NFS_SERVER_IP}:${NFS_SERVER_PATH} ${NFS_MOUNT_PATH}

# 3. 挂载SD卡（硬件存储）
echo "[$(date)] 挂载SD卡: ${SD_CARD_MOUNT_PATH}"
mkdir -p ${SD_CARD_MOUNT_PATH}
mount /dev/mmcblk0p1 ${SD_CARD_MOUNT_PATH}
if [ $? -ne 0 ]; then
    echo "[$(date)] 错误：SD卡挂载失败！"
fi

# 4. 创建人脸抓拍目录
echo "[$(date)] 创建存储目录: ${FACE_CAPTURE_PATH}"
mkdir -p ${FACE_CAPTURE_PATH}

# 5. 配置动态库环境变量（产品化标准路径）
echo "[$(date)] 配置动态库环境变量"
export LD_LIBRARY_PATH=${APP_BASE_DIR}:${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH=${APP_BASE_DIR}/opencv:${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH=${APP_BASE_DIR}/libjpeg:${LD_LIBRARY_PATH}

# 6. 切换工作目录
cd ${APP_BASE_DIR}

# ========================== 【进程监控死循环】 ==========================
echo "============================================================="
echo "[$(date)] 进程监控启动，监控程序: ${APP_NAME}"
echo "============================================================="

while true
do
    # 健壮性查询进程PID（过滤grep/awk自身进程）
    PID=$(ps -ef | grep "${APP_NAME}" | grep -v grep | grep -v awk | awk '{print $1}')

    if [ -z "${PID}" ]; then
        echo "[$(date)] 警告：${APP_NAME} 已崩溃，正在自动重启..."
        # 后台启动程序（不阻塞监控脚本）
        ${APP_PATH} &
    fi

    # 2秒检测一次（工业标准检测间隔）
    sleep 2
done