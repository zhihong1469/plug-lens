---
name: build-linux-app
description: 当需要配置或构建基于 CMake/Makefile 的 Linux 应用工程（如 RK3562 开发板）时使用，支持交叉编译和本地编译。
---

# 构建 Linux 应用

## 适用场景

- `Project Profile` 中标明 `build_system: cmake` 或 `build_system: makefile`，且目标平台为 Linux。
- 用户希望对 Linux 应用工程执行配置、编译或确认产物。
- 需要为 ARM64 开发板（如 RK3562）进行交叉编译。
- 需要在构建前确认环境是否就绪（编译器、工具链）。

## 必要输入

- 工作区路径，或一份已有的 `Project Profile`。
- 可选的构建目录、目标架构（arm64/x86_64）、构建类型和工具链路径。

## 自动探测

- 若存在 `CMakePresets.json`，优先使用脚本的 `--list-presets` 列出并选择预设。
- 检查是否存在交叉编译工具链（如 `aarch64-linux-gnu-gcc`）。
- 自动检测目标架构：通过环境变量、工具链文件或用户指定。
- 对调试导向请求默认使用 `Debug`，否则默认使用 `Release`。

## 执行步骤

1. 先阅读 [references/usage.md](references/usage.md)，确认本次是环境探测、列出预设、执行构建，还是仅扫描产物。
2. 运行脚本 [scripts/linux_builder.py](scripts/linux_builder.py) 的 `--detect` 模式确认环境。
3. 根据目标架构选择编译模式：
   - ARM64 目标：使用交叉编译工具链
   - x86_64 目标：使用本地编译器
4. 执行 cmake configure + build 或直接 make。
5. 读取脚本输出的构建结果和产物扫描报告。
6. 将构建目录、产物路径和工具链信息写回 `Project Profile`。

## 失败分流

- 当缺少编译器或所需工具时，返回 `environment-missing`。
- 当配置或构建因预设损坏、缺失工具链文件而失败时，返回 `project-config-error`。
- 当构建看似成功但未找到可执行文件时，返回 `artifact-missing`。
- 当存在多个同样合理的预设且任意选择都不安全时，返回 `ambiguous-context`。

## 平台说明

- 在 Linux 上优先使用系统编译器，支持交叉编译工具链。
- 在 Windows 上支持 WSL 环境或交叉编译工具链。
- 输出中的构建目录应保持为绝对路径。

## 输出约定

- 输出配置命令、构建命令、构建目录和产物路径。
- 用 `artifact_path`、`artifact_kind` 和工具链细节更新 `Project Profile`。
- 成功后推荐 `debug-linux-app` 进行调试。

## 交接关系

- 当下一步需要调试会话时，将成功构建结果交给 `debug-linux-app`。