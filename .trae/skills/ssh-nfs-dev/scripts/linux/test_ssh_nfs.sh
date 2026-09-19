#!/bin/bash
# SSH + NFS 开发环境快速测试脚本

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)

echo "=========================================="
echo "  SSH + NFS 开发环境测试"
echo "=========================================="
echo ""

# 测试1：检测环境
echo "1. 检测网络和SSH状态..."
python3 "$PROJECT_ROOT/.trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py" detect

echo ""
echo "2. 测试SSH连接..."
if ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no root@192.168.5.11 "echo SSH_OK" 2>/dev/null | grep -q SSH_OK; then
    echo "   ✅ SSH连接正常"
else
    echo "   ❌ SSH连接失败"
fi

echo ""
echo "3. 测试NFS挂载..."
ssh root@192.168.5.11 "mount | grep nfs" 2>/dev/null | grep -q "/mnt/nfs" && echo "   ✅ NFS已挂载" || echo "   ⚠️  NFS未挂载"

echo ""
echo "4. 列出开发板/mnt/nfs目录..."
python3 "$PROJECT_ROOT/.trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py" list /mnt/nfs

echo ""
echo "=========================================="
echo "  测试完成"
echo "=========================================="
echo ""
echo "提示："
echo "  - 如果NFS未挂载，请执行："
echo "    ssh root@192.168.5.11"
echo "    mount_nfs_wired"
echo ""
echo "  - 完整使用文档请查看："
echo "    .trae/skills/ssh-nfs-dev/references/usage.md"