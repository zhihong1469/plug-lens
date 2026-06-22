# Serial Communication Skill 使用指南

## 快速开始

### 1. 环境准备

#### Python 依赖安装

```bash
# 核心依赖
pip install pyserial

# Linux 平台推荐安装（用于交互式登录）
pip install pexpect
sudo apt-get install picocom
```

### 2. 基本使用

#### 自动检测串口并验证

```bash
# WSL2/Linux 环境
# 自动检测串口，执行 uname -a
python scripts/linux/serial_tool.py login-check

# 自动检测串口，执行自定义命令
python scripts/linux/serial_tool.py login-check --cmd "ls /mnt/nfs && ./myapp"
```

#### 指定串口设备

```bash
# WSL2/Linux 平台
python scripts/linux/serial_tool.py login-check --port /dev/ttyUSB0 --cmd "uname -a"

# Windows 平台（直接连接）
python scripts/linux/serial_tool.py login-check --port COM3 --cmd "uname -a"
```

#### 带密码登录

```bash
python scripts/linux/serial_tool.py login-check \
  --port /dev/ttyUSB0 \
  --username root \
  --password 123456 \
  --cmd "cd /mnt/nfs && ./myapp"
```

### 3. 查看可用串口

```bash
# WSL2/Linux 环境
# 列出所有可用串口
python scripts/linux/serial_tool.py list-ports

# 查看特定串口详细信息
python scripts/linux/serial_tool.py port-info --port /dev/ttyUSB0
```

### 4. WSL2 USB 转发（Windows 环境）

```bash
# 打开管理员 PowerShell，执行以下命令：

# 列出所有 USB 设备
python scripts/windows/wsl2_usb_forward.py list

# 自动检测并转发串口设备（推荐）
python scripts/windows/wsl2_usb_forward.py auto

# 手动指定 BUSID 转发
python scripts/windows/wsl2_usb_forward.py forward --busid 3-3

# 分离已转发的设备
python scripts/windows/wsl2_usb_forward.py detach --busid 3-3
```

---

## Python API 使用

### 1. 自动登录验证

```python
# WSL2/Linux 环境
from scripts.shared import run_login_check

# 无密码自动登录
result = run_login_check(
    port="",           # 自动检测
    baudrate=115200,
    username="",       # 无密码
    password="",
    cmd="uname -a",
    wait_sec=120
)

if result == 0:
    print("验证成功")
else:
    print("验证失败")
```

### 2. 带密码登录

```python
from scripts.shared import run_login_check

result = run_login_check(
    port="/dev/ttyUSB0",
    baudrate=115200,
    username="root",
    password="123456",
    cmd="cd /mnt/nfs && ./myapp && echo APP_EXIT:$?",
    wait_sec=60
)
```

### 3. 串口设备发现

```python
from scripts.shared import list_candidate_ports, resolve_port

# 列出候选串口
ports = list_candidate_ports()
print(f"可用串口: {ports}")

# 自动选择串口
port = resolve_port()
print(f"选择串口: {port}")

# 指定串口（如果不存在则自动选择）
port = resolve_port("/dev/ttyUSB1")
```

### 4. 直接串口会话

```python
from scripts.shared import SerialSession, make_serial_config

# 创建配置
cfg = make_serial_config(
    port="/dev/ttyUSB0",
    baudrate=115200,
    timeout=1.0
)

# 创建会话
session = SerialSession(cfg)
session.open()

# 发送命令并读取响应
output = session.run_command("uname -a")
print(output)

# 关闭连接
session.close()
```

### 5. WSL2 USB 转发（Windows 环境）

```python
# Windows 环境专用
from scripts.windows.wsl2_usb_forward import (
    get_windows_usb_devices,
    find_device_by_vid_pid,
    forward_device
)

# 获取 USB 设备列表
devices = get_windows_usb_devices()
for dev in devices:
    print(f"{dev['busid']}: {dev['name']} ({dev['vid']}:{dev['pid']})")

# 查找 CH340 设备
ch340_device = find_device_by_vid_pid("1a86", "7523")
if ch340_device:
    print(f"找到 CH340: {ch340_device['busid']}")
    # 转发到 WSL2
    forward_device(ch340_device['busid'])
```

---

## 常见场景

### 场景1：NFS 挂载程序验证

```bash
# 验证 NFS 挂载并运行程序（WSL2/Linux）
python scripts/linux/serial_tool.py login-check \
  --cmd "ls /mnt/nfs; cd /mnt/nfs && ./myapp --version"
```

### 场景2：开发板状态检查

```bash
# 检查系统信息、内存、进程（WSL2/Linux）
python scripts/linux/serial_tool.py login-check \
  --cmd "uname -a; free -m; ps aux | head -n 20"
```

### 场景3：程序运行日志捕获

```bash
# 运行程序并捕获日志（WSL2/Linux）
python scripts/linux/serial_tool.py login-check \
  --cmd "cd /mnt/nfs && ./myapp 2>&1 | tee app.log" \
  --wait-sec 300
```

### 场景4：多板子环境指定串口

```bash
# 指定特定板子的串口（通过 VID/PID）（WSL2/Linux）
python scripts/linux/serial_tool.py port-info --port /dev/ttyUSB0
# 根据输出信息确定目标板子

python scripts/linux/serial_tool.py login-check --port /dev/ttyUSB1 --cmd "hostname"
```

### 场景5：WSL2 USB 转发工作流

```bash
# Step 1: 在 Windows 管理员终端中转发 USB
python scripts/windows/wsl2_usb_forward.py auto

# Step 2: 在 WSL2 中验证串口
python scripts/linux/serial_tool.py list-ports

# Step 3: 在 WSL2 中连接开发板
python scripts/linux/serial_tool.py login-check --cmd "uname -a"
```

---

## 参数说明

### login-check 命令参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--port` | "" | 串口路径，空则自动检测 |
| `--baudrate` | 115200 | 波特率 |
| `--username` | "" | 登录用户名，空则使用无密码模式 |
| `--password` | "" | 登录密码 |
| `--cmd` | "uname -a" | 登录后执行的命令 |
| `--wait-sec` | 120 | 最大等待时间（秒） |
| `--attempt-interval-sec` | 2.0 | 重试间隔（秒） |

---

## 错误处理

### 常见错误及解决方案

| 错误信息 | 原因 | 解决方案 |
|---------|------|---------|
| `No serial port found` | 未检测到串口设备 | 检查 USB 连接，确认驱动已加载 |
| `Login timeout` | 未检测到登录提示符 | 检查波特率，确认板子已启动 |
| `Command timeout` | 命令执行超时 | 增加 `--wait-sec` 参数 |
| `pexpect not installed` | 缺少 pexpect 库 | `pip install pexpect` |
| `picocom not installed` | 缺少 picocom 工具 | `sudo apt-get install picocom` |

### 错误返回码

| 返回码 | 说明 |
|--------|------|
| 0 | 成功 |
| 1 | 操作失败（超时、连接错误等） |
| 2 | 环境缺失（缺少依赖库或工具） |

---

## 平台差异

### Linux 平台

- 支持 `picocom` + `pexpect` 交互式登录
- 串口设备路径：`/dev/ttyACM*`, `/dev/ttyUSB*`
- 推荐安装：`picocom`, `pexpect`

### Windows 平台

- 使用 `pyserial` 直接连接
- 串口设备路径：`COM*`
- `pexpect` 功能受限，使用直接串口方式

---

## 与其他技能集成

### 与 build-linux-app 集成

```python
# 编译完成后自动验证
from skills.build_linux_app import build_project
from skills.serial_communication import run_login_check

# 1. 编译项目
build_result = build_project()

# 2. 串口验证
if build_result.success:
    verify_result = run_login_check(
        cmd=f"cd /mnt/nfs && ./myapp --version"
    )
```

### 与 board-workflow 集成

```bash
# 作为自动化流程的一部分
# workflow.sh
python scripts/serial_cli.py login-check --cmd "mount -t nfs 192.168.1.100:/nfs /mnt/nfs"
python scripts/serial_cli.py login-check --cmd "cd /mnt/nfs && ./myapp"
```

---

## 最佳实践

### 1. 验证命令设计

```bash
# 好的验证命令：明确、有输出、可判断
--cmd "uname -a; ls -l /mnt/nfs/myapp; ./myapp --version"

# 不好的验证命令：模糊、无输出、难判断
--cmd "run myapp"
```

### 2. 超时时间设置

```python
# 快速验证：60秒
wait_sec=60

# 程序运行：300秒
wait_sec=300

# 系统启动等待：120秒
wait_sec=120
```

### 3. 错误处理

```python
result = run_login_check(cmd="myapp")

if result == 0:
    print("验证成功")
elif result == 1:
    print("验证失败，检查串口连接或命令")
elif result == 2:
    print("环境缺失，安装依赖库")
```

---

## 安全注意事项

1. **禁止自动执行危险命令**：`rm -rf`, `dd`, `reboot` 等
2. **禁止自动执行特权命令**：`sudo`, `chmod 777` 等
3. **设置合理超时**：避免无限等待
4. **及时关闭连接**：操作完成后关闭串口

---

## 参考资料

- [serial_core.py](../scripts/serial_core.py) - 串口核心功能实现
- [discovery.py](../scripts/discovery.py) - 设备发现逻辑
- [login_check.py](../scripts/login_check.py) - 自动登录验证
- [serial_pexpect.py](../scripts/serial_pexpect.py) - pexpect 终端封装