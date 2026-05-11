import sys
import os
# 导入编译好的OpenCV
project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
cv_module_path = os.path.join(project_root, "third_lib/opencv_local/lib/python3.12/dist-packages")
sys.path.insert(0, cv_module_path)

import socket
import cv2
import numpy as np

# 配置
BOARD_IP = "192.168.5.9"
BOARD_PORT = 8888
WIDTH = 640
HEIGHT = 360
FRAME_SIZE = WIDTH * HEIGHT * 2
HEADER_SIZE = 28
MAGIC = 0x12345678

# 创建UDP客户端
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2)
sock.sendto(b"CONNECT", (BOARD_IP, BOARD_PORT))
print(f"连接开发板: {BOARD_IP}:{BOARD_PORT}，等待视频流...")

frame_count = 0
while True:
    try:
        # 接收帧头
        header, _ = sock.recvfrom(HEADER_SIZE)
        if len(header) != HEADER_SIZE:
            continue
        magic = int.from_bytes(header[:4], 'little')
        if magic != MAGIC:
            continue

        # 接收完整帧
        data = b""
        while len(data) < FRAME_SIZE:
            packet, _ = sock.recvfrom(FRAME_SIZE - len(data))
            data += packet

        # YUYV转BGR（核心图像处理正常运行）
        yuyv = np.frombuffer(data, dtype=np.uint8).reshape((HEIGHT, WIDTH, 2))
        bgr = cv2.cvtColor(yuyv, cv2.COLOR_YUV2BGR_YUYV)

        # ✅ 不显示窗口，改为保存图片（验证视频流正常）
        frame_count += 1
        if frame_count % 10 == 0:
            cv2.imwrite("udp_frame.jpg", bgr)
            print(f"✅ 接收第 {frame_count} 帧，已保存图片: udp_frame.jpg")

    except socket.timeout:
        continue
    except KeyboardInterrupt:
        print("\n退出程序")
        break

sock.close()