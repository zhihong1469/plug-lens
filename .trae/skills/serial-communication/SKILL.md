---
name: serial-communication
description: "Serial communication skill for embedded Linux boards. Provides auto-detection, login verification, and command execution. Invoke when user needs serial port interaction, board verification, or command execution via serial console."
---

# Serial Communication Skill

## 目录结构

```shell
.trae/skills/serial-communication/
├── SKILL.md                      # 技能定义文件
├── references/
│   └── usage.md                  # 详细使用指南
└── scripts/
    ├── linux/                    # WSL2/Linux 专用脚本
    │   ├── __init__.py
    │   └── serial_tool.py        # 串口通信工具（CLI）
    ├── shared/                   # 跨平台共享代码
    │   ├── __init__.py
    │   ├── discovery.py          # 串口设备发现
    │   ├── login_check.py        # 自动登录验证
    │   ├── serial_core.py        # 串口核心功能
    │   └── serial_pexpect.py     # pexpect 终端封装
    ├── windows/                  # Windows 专用脚本
    │   └── wsl2_usb_forward.py   # WSL2 USB 转发工具
    └── requirements.txt          # Python 依赖
```
## 适用场景

- 需要通过串口连接嵌入式 Linux 开发板（如 RK3562）
- 需要自动检测可用的串口设备
- 需要自动登录开发板并执行验证命令
- 需要通过串口进行程序运行验证
- 需要获取开发板的运行状态或日志输出

## 核心能力

### 1. 串口设备自动发现
- 自动扫描系统中的可用串口（Linux: `/dev/ttyACM*`, `/dev/ttyUSB*`; Windows: `COM*`)
- 支持按 VID/PID/序列号/产品名称筛选
- 提供详细的设备信息查询

### 2. 自动登录验证
- 支持无密码自动登录模式（检测 login 提示符）
- 支持带密码的显式登录模式（使用 pexpect）
- 自动检测 Shell 提示符并执行命令
- 支持超时重试机制

### 3. 命令执行与输出捕获
- 发送命令并等待响应
- 实时捕获输出日志
- 支持长时间运行的命令
- 自动处理命令执行结果

## 执行流程

### 基本使用流程

```
1. 探测串口设备 → scripts/discovery.py
2. 选择目标串口 → resolve_port()
3. 建立串口连接 → SerialSession.open()
4. 自动登录验证 → login_check.py
5. 执行验证命令 → run_command()
6. 捕获输出结果 → read_available_text()
7. 关闭连接 → close()
```

### 自动化验证流程

```
用户请求验证 → 
  自动检测串口 → 
    建立连接 → 
      检测登录提示 → 
        发送用户名 → 
          检测 Shell 提示符 → 
            执行验证命令 → 
              返回结果
```

## 必要输入

- **串口路径**（可选）：如 `/dev/ttyUSB0` 或 `COM3`，不提供则自动检测
- **波特率**（可选）：默认 115200
- **用户名/密码**（可选）：用于需要密码登录的板子
- **验证命令**（可选）：默认 `uname -a`
- **等待时间**（可选）：默认 120 秒

## 自动探测

- 自动扫描系统串口设备列表
- 优先选择 USB 转串口设备（ttyACM/ttyUSB/COM）
- 支持按设备属性筛选（VID/PID/序列号）
- 自动检测登录提示符和 Shell 提示符

## 输出约定

### 成功输出格式

```
[serial-check] selected port: /dev/ttyUSB0
[serial-check] detected login prompt -> send root
[serial-check] detected shell prompt -> send command: uname -a
Linux rk3562 5.4.61 #1 SMP PREEMPT ...
root@rk3562:~#
```

### 失败输出格式

```
ERROR: No serial port found. Please check USB connection or specify port manually.
ERROR: timed out waiting for login prompt or command output
```

## 平台兼容性

### Linux 平台
- 使用 `picocom` 作为串口终端（需要安装）
- 使用 `pexpect` 进行交互式登录
- 支持 `/dev/ttyACM*` 和 `/dev/ttyUSB*`

### Windows 平台
- 使用 `pyserial` 直接连接
- 支持 `COM*` 端口
- pexpect 功能受限，使用直接串口方式

## 依赖工具

### Python 包
- `pyserial` - 串口通信核心库
- `pexpect` - 交互式终端（Linux 推荐）

### Linux 系统工具
- `picocom` - 串口终端程序（可选）

## 使用示例

### Python API 调用

```python
# WSL2/Linux 环境
from scripts.shared import SerialSession, make_serial_config, run_login_check

# 方式1：自动检测并验证
result = run_login_check(
    port="",           # 自动检测
    baudrate=115200,
    username="",       # 无密码登录
    password="",
    cmd="uname -a; ls /mnt/nfs",
    wait_sec=120
)

# 方式2：指定串口并带密码登录
result = run_login_check(
    port="/dev/ttyUSB0",
    baudrate=115200,
    username="root",
    password="123456",
    cmd="cd /mnt/nfs && ./myapp",
    wait_sec=60
)
```

### CLI 命令调用

```bash
# === WSL2/Linux 环境 ===
# 自动检测串口并执行命令
python scripts/linux/serial_tool.py login-check --cmd "uname -a"

# 指定串口和验证命令
python scripts/linux/serial_tool.py login-check --port /dev/ttyUSB0 --cmd "ls /mnt/nfs"

# 带密码登录
python scripts/linux/serial_tool.py login-check --port /dev/ttyUSB0 --username root --password 123456 --cmd "./myapp"

# === Windows 环境（USB 转发）===
# 列出所有 USB 设备（Windows PowerShell）
python scripts/windows/wsl2_usb_forward.py list

# 自动检测并转发串口设备（需要管理员权限）
python scripts/windows/wsl2_usb_forward.py auto

# 手动指定 BUSID 转发
python scripts/windows/wsl2_usb_forward.py forward --busid 3-3
```

## 失败分流

| 错误类型 | 返回值 | 处理建议 |
|---------|--------|---------|
| `No serial port found` | RuntimeError | 检查 USB 连接，确认驱动已加载 |
| `Login timeout` | 1 | 检查波特率设置，确认板子已启动 |
| `Command timeout` | 1 | 增加等待时间，检查命令是否卡住 |
| `pexpect not installed` | 2 | 执行 `pip install pexpect` |
| `picocom not installed` | 2 | 执行 `sudo apt-get install picocom` |

## 与其他技能的交接

### 前置技能
- `build-linux-app` - 编译完成后需要验证
- `flash-deploy` - 烧录完成后需要验证

### 后续技能
- `debug-linux-app` - 验证失败时启动调试
- `board-workflow` - 作为自动化流程的一部分

## 安全约束

- **禁止危险命令**：不得自动执行 `rm -rf`、`dd`、`reboot` 等破坏性命令
- **禁止特权操作**：不得自动执行 `sudo` 等特权命令
- **超时保护**：所有操作必须设置超时，防止无限等待
- **连接保护**：操作完成后必须关闭串口连接

## 参考文档

- [scripts/serial_core.py](scripts/serial_core.py) - 串口核心功能
- [scripts/discovery.py](scripts/discovery.py) - 设备发现逻辑
- [scripts/login_check.py](scripts/login_check.py) - 自动登录验证
- [references/usage.md](references/usage.md) - 详细使用说明