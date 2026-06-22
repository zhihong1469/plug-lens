# SSH + NFS 远程开发使用指南

## 网络配置

```
WSL2 IP:      192.168.5.10
开发板 IP:    192.168.5.11
SSH用户:      root
NFS挂载点:    /mnt/nfs
```

## 快速开始

### 1. 检测环境状态

```bash
python3 .trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py detect
```

输出示例：
```
✅ WSL2 IP: 192.168.5.10
✅ Board 192.168.5.11 is reachable
✅ SSH connection OK
✅ NFS is mounted
```

### 2. 挂载NFS（如果未挂载）

```bash
# 方法1：使用脚本自动挂载
python3 .trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py mount

# 方法2：在开发板上手动挂载（推荐）
ssh root@192.168.5.11
mount_nfs_wired
```

### 3. 同步文件到开发板

```bash
# 同步单个文件
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py sync-file ./build/hello_rk --subdir run_on_board

# 同步整个目录
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py sync-dir ./build/ --subdir run_on_board
```

### 4. 在开发板上运行程序

```bash
# 直接运行
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py run /mnt/nfs/run_on_board/hello_rk

# 后台运行
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py run /mnt/nfs/run_on_board/hello_rk --background

# 检查运行状态
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py check hello_rk

# 终止程序
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py kill hello_rk
```

## 典型开发流程

### 流程1：编译 → 同步 → 运行

```bash
# 1. 编译项目
cd build
cmake ..
make

# 2. 同步到开发板
python3 ../.trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py sync-file ./hello_rk --subdir run_on_board

# 3. 运行
python3 ../.trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py run /mnt/nfs/run_on_board/hello_rk
```

### 流程2：使用NFS直接访问（推荐）

由于NFS已挂载，你可以直接在开发板上访问编译产物：

```bash
# 1. 编译项目（产物在本地 NFS 共享目录）
cd build
cmake ..
make

# 2. SSH到开发板
ssh root@192.168.5.11

# 3. 直接运行（因为NFS已挂载，文件已可见）
cd /mnt/nfs/run_on_board
./hello_rk
```

### 流程3：自动化脚本

创建一个自动化脚本 `deploy_and_run.sh`：

```bash
#!/bin/bash
# 自动编译、同步、运行

PROJECT_ROOT="/home/luo/linux/6ull/project/plug-lens"
BUILD_DIR="$PROJECT_ROOT/build"
PROGRAM_NAME="hello_rk"

# 1. 编译
echo "Building..."
cd $BUILD_DIR
make

# 2. 同步
echo "Syncing..."
python3 $PROJECT_ROOT/.trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py \
    sync-file $BUILD_DIR/$PROGRAM_NAME --subdir run_on_board

# 3. 运行
echo "Running..."
python3 $PROJECT_ROOT/.trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py \
    run /mnt/nfs/run_on_board/$PROGRAM_NAME
```

## 常用命令速查

### SSH管理

| 命令 | 说明 |
|------|------|
| `ssh_manager.py detect` | 检测网络和SSH状态 |
| `ssh_manager.py mount` | 在开发板上挂载NFS |
| `ssh_manager.py unmount` | 卸载NFS |
| `ssh_manager.py exec "uname -a"` | 执行远程命令 |
| `ssh_manager.py list /mnt/nfs` | 列出远程目录 |

### 文件同步和执行

| 命令 | 说明 |
|------|------|
| `nfs_sync_exec.py sync-file FILE` | 同步单个文件 |
| `nfs_sync_exec.py sync-dir DIR` | 同步整个目录 |
| `nfs_sync_exec.py run PROGRAM` | 运行程序 |
| `nfs_sync_exec.py run PROGRAM --background` | 后台运行 |
| `nfs_sync_exec.py check PROGRAM` | 检查运行状态 |
| `nfs_sync_exec.py kill PROGRAM` | 终止程序 |

## 开发板预设目录

```
/mnt/nfs/
├── run_on_board/          # 通用运行目录
├── run_on_board_rk3562/   # RK3562专用运行目录
├── test/                  # 测试目录
├── sdcard/                # SD卡挂载点
└── udisk/                 # U盘挂载点
```

## 故障排查

### SSH连接失败

```bash
# 检查网络连通性
ping 192.168.5.11

# 检查SSH服务
ssh root@192.168.5.11 "systemctl status sshd"
```

### NFS挂载失败

```bash
# 检查NFS服务（在WSL2上）
sudo systemctl status nfs-server

# 检查防火墙
sudo ufw status

# 手动挂载测试
ssh root@192.168.5.11
mount -t nfs 192.168.5.10:/home/luo/linux/6ull/project/plug-lens /mnt/nfs
```

### 文件同步失败

```bash
# 检查文件权限
ls -la ./build/hello_rk

# 检查远程目录权限
ssh root@192.168.5.11 "ls -la /mnt/nfs/run_on_board"
```

## 与串口配合使用

### 最佳实践

```
1. 串口：查看启动日志和调试信息
2. SSH + NFS：日常开发和文件传输
3. 串口：网络问题时紧急调试
```

### 示例场景

**场景：调试程序崩溃**

```bash
# 1. 通过SSH运行程序（失败）
python3 nfs_sync_exec.py run /mnt/nfs/run_on_board/hello_rk

# 2. 通过串口查看dmesg日志
python3 ../serial-communication/scripts/linux/serial_tool.py login-check --cmd "dmesg | tail -20"

# 3. 分析问题后修复代码，重新编译同步
make
python3 nfs_sync_exec.py sync-file ./hello_rk --subdir run_on_board
```

## 高级用法

### 批量同步

```bash
# 同步所有编译产物
python3 nfs_sync_exec.py sync-dir ./build/ --subdir run_on_board_rk3562
```

### 远程调试

```bash
# 启动gdbserver（在开发板上）
ssh root@192.168.5.11 "gdbserver :1234 /mnt/nfs/run_on_board/hello_rk"

# 在WSL2上连接gdb
gdb-multiarch ./build/hello_rk
target remote 192.168.5.11:1234
```

### 性能测试

```bash
# 运行性能测试程序
python3 nfs_sync_exec.py run /mnt/nfs/test/perf_test --args "--iterations 1000"
```