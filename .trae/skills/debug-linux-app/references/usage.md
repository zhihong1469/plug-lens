# debug-linux-app 使用说明

## 功能概述

本技能用于调试 Linux 应用程序，支持本地调试和远程 GDB 调试。

## 支持的调试模式

- 本地调试：直接使用 gdb 调试本地可执行文件
- 远程调试：通过 gdbserver 调试目标板上的程序

## 工具要求

- gdb / gdb-multiarch（本地调试）
- aarch64-linux-gnu-gdb（ARM64 交叉调试）
- gdbserver（远程目标）

## 使用示例

### 1. 环境探测
```bash
python3 scripts/linux_debugger.py --detect
```

### 2. 本地调试
```bash
python3 scripts/linux_debugger.py --executable ./build/app --local
```

### 3. 远程调试
```bash
python3 scripts/linux_debugger.py --executable ./build/app \
    --remote --target-ip 192.168.1.100 --target-port 1234
```

### 4. 设置断点并调试
```bash
python3 scripts/linux_debugger.py --executable ./build/app \
    --break main --break func.cpp:42
```

## 输出示例

```json
{
    "status": "success",
    "summary": "调试会话已启动",
    "debugger": "gdb-multiarch",
    "executable": "/path/to/app",
    "debug_mode": "remote",
    "target_ip": "192.168.1.100",
    "target_port": 1234,
    "breakpoints": ["main", "func.cpp:42"],
    "commands": ["target remote 192.168.1.100:1234", "break main", "run"]
}
```

## 常见问题

### Q: 如何在目标板上启动 gdbserver？
A: 在目标板上执行：
```bash
gdbserver :1234 /path/to/app
```

### Q: 如何启用远程调试？
A: 确保目标板上已安装 gdbserver，并且网络可达。