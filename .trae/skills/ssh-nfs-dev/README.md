# SSH + NFS 远程开发技能系统总结

## 📁 技能目录结构

```
.trae/skills/ssh-nfs-dev/
├── SKILL.md                          # 技能定义文档
├── .skill_meta.json                  # 元数据
├── scripts/
│   └── linux/
│       ├── ssh_manager.py            # SSH连接管理脚本
│       ├── nfs_sync_exec.py          # 文件同步和远程执行脚本
│       └── test_ssh_nfs.sh           # 快速测试脚本
├── references/
│   └── usage.md                      # 详细使用文档
└── agents/
    └── (预留)
```

## 🎯 核心功能

### 1. SSH连接管理 (`ssh_manager.py`)

**功能列表**：
- `detect`: 检测网络和SSH状态
- `mount`: 在开发板上挂载NFS
- `unmount`: 卸载NFS
- `exec`: 执行远程命令
- `list`: 列出远程目录

**使用示例**：
```bash
# 检测环境
python3 .trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py detect

# 挂载NFS
python3 .trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py mount

# 执行命令
python3 .trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py exec "uname -a"
```

### 2. 文件同步和远程执行 (`nfs_sync_exec.py`)

**功能列表**：
- `sync-file`: 同步单个文件
- `sync-dir`: 同步整个目录
- `run`: 运行程序
- `kill`: 终止程序
- `check`: 检查运行状态

**使用示例**：
```bash
# 同步文件
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py sync-file ./build/hello_rk --subdir run_on_board

# 运行程序
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py run /mnt/nfs/run_on_board/hello_rk

# 后台运行
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py run /mnt/nfs/run_on_board/hello_rk --background
```

## 🔧 网络配置

```
WSL2 IP:      192.168.5.10 (eth2接口)
开发板 IP:    192.168.5.11
SSH用户:      root
NFS挂载点:    /mnt/nfs
```

## 🚀 快速开始

### 步骤1：设置SSH密钥认证（推荐）

为了避免每次都输入密码，建议设置SSH密钥认证：

```bash
# 1. 生成SSH密钥（如果还没有）
ssh-keygen -t rsa -b 4096

# 2. 复制公钥到开发板
ssh-copy-id root@192.168.5.11

# 3. 测试免密登录
ssh root@192.168.5.11 "uname -a"
```

### 步骤2：检测环境

```bash
python3 .trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py detect
```

预期输出：
```
✅ WSL2 IP: 192.168.5.10
✅ Board 192.168.5.11 is reachable
✅ SSH connection OK
✅ NFS is mounted
```

### 步骤3：挂载NFS（如果未挂载）

```bash
# 方法1：使用脚本（需要SSH密钥）
python3 .trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py mount

# 方法2：手动挂载（推荐）
ssh root@192.168.5.11
mount_nfs_wired
```

### 步骤4：开始开发

```bash
# 编译
cd build
cmake ..
make

# 同步到开发板
python3 ../.trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py sync-file ./hello_rk --subdir run_on_board

# 运行
python3 ../.trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py run /mnt/nfs/run_on_board/hello_rk
```

## 📊 与串口配合使用

### 最佳实践流程

```
┌─────────────────────────────────────────────────────────┐
│              完整嵌入式开发流程                           │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  1️⃣  串口连接（开发板启动时）                          │
│      ├─ 查看uboot和kernel启动日志                       │
│      ├─ 确认网络配置和IP地址                            │
│      └─ 调试启动问题                                    │
│                                                         │
│  2️⃣  SSH + NFS（日常开发）                             │
│      ├─ 编译项目                                        │
│      ├─ 自动同步到开发板                                │
│      ├─ 远程执行和测试                                  │
│      └─ 快速迭代开发                                    │
│                                                         │
│  3️⃣  串口调试（问题时）                                │
│      ├─ 查看dmesg日志                                   │
│      ├─ 网络故障排查                                    │
│      └─ 系统级调试                                      │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### 典型场景示例

**场景1：新项目开发**

```bash
# 1. 串口查看启动日志
python3 .trae/skills/serial-communication/scripts/linux/serial_tool.py login-check

# 2. 确认开发板IP后，SSH连接
python3 .trae/skills/ssh-nfs-dev/scripts/linux/ssh_manager.py detect

# 3. 挂载NFS
ssh root@192.168.5.11 "mount_nfs_wired"

# 4. 开始开发
cd build && make
python3 ../.trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py sync-file ./hello_rk --subdir run_on_board
python3 ../.trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py run /mnt/nfs/run_on_board/hello_rk
```

**场景2：程序崩溃调试**

```bash
# 1. SSH运行程序（失败）
python3 .trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py run /mnt/nfs/run_on_board/hello_rk

# 2. 串口查看崩溃日志
python3 .trae/skills/serial-communication/scripts/linux/serial_tool.py login-check --cmd "dmesg | tail -30"

# 3. 修复代码，重新编译同步
cd build && make
python3 ../.trae/skills/ssh-nfs-dev/scripts/linux/nfs_sync_exec.py sync-file ./hello_rk --subdir run_on_board
```

## 📝 常用命令速查表

### SSH管理命令

| 命令 | 说明 |
|------|------|
| `ssh_manager.py detect` | 检测网络和SSH状态 |
| `ssh_manager.py mount` | 挂载NFS |
| `ssh_manager.py unmount` | 卸载NFS |
| `ssh_manager.py exec "cmd"` | 执行远程命令 |
| `ssh_manager.py list /path` | 列出远程目录 |

### 文件同步和执行命令

| 命令 | 说明 |
|------|------|
| `nfs_sync_exec.py sync-file FILE` | 同步单个文件 |
| `nfs_sync_exec.py sync-dir DIR` | 同步整个目录 |
| `nfs_sync_exec.py run PROGRAM` | 运行程序 |
| `nfs_sync_exec.py run PROGRAM --background` | 后台运行 |
| `nfs_sync_exec.py check PROGRAM` | 检查运行状态 |
| `nfs_sync_exec.py kill PROGRAM` | 终止程序 |

### 串口命令（配合使用）

| 命令 | 说明 |
|------|------|
| `serial_tool.py list-ports` | 列出串口设备 |
| `serial_tool.py login-check` | 串口登录和验证 |

## 🔍 故障排查

### SSH连接失败

**问题**：SSH连接需要密码或失败

**解决方案**：
```bash
# 1. 设置SSH密钥认证
ssh-keygen -t rsa -b 4096
ssh-copy-id root@192.168.5.11

# 2. 测试连接
ssh root@192.168.5.11 "uname -a"
```

### NFS挂载失败

**问题**：开发板上NFS未挂载

**解决方案**：
```bash
# 1. 检查WSL2 NFS服务
sudo systemctl status nfs-server

# 2. 在开发板上手动挂载
ssh root@192.168.5.11
mount_nfs_wired
```

### 文件同步失败

**问题**：SCP或rsync失败

**解决方案**：
```bash
# 1. 检查文件权限
ls -la ./build/hello_rk

# 2. 检查远程目录权限
ssh root@192.168.5.11 "ls -la /mnt/nfs/run_on_board"

# 3. 手动复制测试
scp ./build/hello_rk root@192.168.5.11:/mnt/nfs/run_on_board/
```

## 📚 更多文档

- **详细使用指南**：[.trae/skills/ssh-nfs-dev/references/usage.md](file:///home/luo/linux/6ull/project/plug-lens/.trae/skills/ssh-nfs-dev/references/usage.md)
- **技能定义**：[.trae/skills/ssh-nfs-dev/SKILL.md](file:///home/luo/linux/6ull/project/plug-lens/.trae/skills/ssh-nfs-dev/SKILL.md)

## 🎉 总结

你现在拥有了一个完整的SSH + NFS远程开发技能系统，可以：

1. ✅ 自动检测网络和SSH连接状态
2. ✅ 管理NFS挂载
3. ✅ 快速同步文件到开发板
4. ✅ 远程执行程序
5. ✅ 与串口技能配合使用

**建议下一步**：
- 设置SSH密钥认证实现免密登录
- 创建自动化部署脚本
- 结合IDE的SSH远程挂载功能

祝你开发顺利！🚀