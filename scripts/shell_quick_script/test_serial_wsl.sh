#!/bin/bash
# WSL2串口设备快速测试脚本
# 用于验证USB转发是否正常工作

echo "=========================================="
echo "  WSL2 串口设备测试脚本"
echo "=========================================="
echo ""

# 1. 检查USB设备
echo "1. 检查USB设备..."
lsusb | grep -i ch340 || echo "   ⚠️  CH340设备未找到"

echo ""

# 2. 检查串口设备
echo "2. 检查串口设备..."
if [ -e /dev/ttyUSB0 ]; then
    echo "   ✅ /dev/ttyUSB0 存在"
    ls -la /dev/ttyUSB0
else
    echo "   ❌ /dev/ttyUSB0 不存在"
fi

echo ""

# 3. 检查用户权限
echo "3. 检查用户权限..."
groups $USER | grep -q dialout && echo "   ✅ 用户在 dialout 组中" || echo "   ❌ 用户不在 dialout 组中"

echo ""

# 4. 检查最近的内核日志
echo "4. 检查最近的内核日志（CH340相关）..."
dmesg | grep -i ch340 | tail -3 || echo "   ⚠️  没有找到CH340相关日志"

echo ""
echo "=========================================="
echo "  测试完成"
echo "=========================================="
echo ""
echo "提示："
echo "  - 如果设备不存在，请重新在Windows端执行:"
echo "    usbipd bind --busid=3-3 --force"
echo "    usbipd attach --wsl --busid=3-3"
echo ""
echo "  - 如果权限不足，请执行:"
echo "    sudo usermod -a -G dialout \$USER"
echo "    然后重新登录"
