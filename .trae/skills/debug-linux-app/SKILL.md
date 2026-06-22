---
name: debug-linux-app
description: 当需要调试 Linux 应用程序时使用，支持本地调试和远程 GDB 调试（如 RK3562 开发板）。
---

# 调试 Linux 应用

## 适用场景

- 用户需要调试 Linux 应用程序。
- 需要在本地或远程目标（如 RK3562）上进行 GDB 调试。
- 需要设置断点、查看变量、堆栈跟踪等调试操作。
- 需要附加到正在运行的进程进行调试。

## 必要输入

- 工作区路径，或一份已有的 `Project Profile`。
- 目标可执行文件路径。
- 可选的远程目标 IP 地址和端口。

## 自动探测

- 自动检测 GDB 工具是否可用（gdb、gdb-multiarch、aarch64-linux-gnu-gdb）。
- 检测目标可执行文件是否存在且有调试符号。
- 检测是否需要远程调试（通过配置或用户指定）。

## 执行步骤

1. 先阅读 [references/usage.md](references/usage.md)，确认调试模式。
2. 运行脚本 [scripts/linux_debugger.py](scripts/linux_debugger.py) 的 `--detect` 模式确认环境。
3. 根据目标类型选择调试方式：
   - 本地调试：直接启动 gdb
   - 远程调试：配置 gdbserver 并连接
4. 设置断点并启动调试会话。

## 失败分流

- 当缺少 gdb 工具时，返回 `environment-missing`。
- 当目标可执行文件不存在或无调试符号时，返回 `project-config-error`。
- 当远程连接失败时，返回 `connection-error`。

## 平台说明

- 支持本地调试（x86_64）和远程调试（ARM64）。
- 支持 gdb-multiarch 进行多架构调试。

## 输出约定

- 输出调试命令和会话信息。
- 更新 `Project Profile` 中的调试配置。

## 交接关系

- 调试完成后推荐 `serial-monitor` 查看程序输出。