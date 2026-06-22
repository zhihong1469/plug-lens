# build-linux-app 使用说明

## 功能概述

本技能用于构建 Linux 应用程序，特别支持 ARM64 交叉编译场景（如 RK3562 开发板）。

## 支持的构建系统

- CMake（优先推荐）
- Makefile

## 支持的目标架构

- x86_64（本地编译）
- arm64（交叉编译）

## 工具链要求

### 本地编译（x86_64）
- gcc/g++
- cmake（3.10+）
- make/ninja

### 交叉编译（arm64）
- aarch64-linux-gnu-gcc/g++
- 或自定义工具链

## 使用示例

### 1. 环境探测
```bash
python3 scripts/linux_builder.py --detect
```

### 2. 列出 CMake 预设
```bash
python3 scripts/linux_builder.py --source . --list-presets
```

### 3. 使用预设构建（arm64）
```bash
python3 scripts/linux_builder.py --source . --preset arm64-release
```

### 4. 手动配置构建
```bash
python3 scripts/linux_builder.py --source . --build-dir build/arm64 \
    --arch arm64 --build-type Release
```

### 5. Makefile 构建
```bash
python3 scripts/linux_builder.py --source . --build-system makefile
```

## 输出示例

```json
{
    "status": "success",
    "summary": "构建成功",
    "configure_cmd": "cmake -S . -B build/arm64 -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake",
    "build_cmd": "cmake --build build/arm64 -j8",
    "build_dir": "/path/to/project/build/arm64",
    "artifacts": [
        {"path": "/path/to/project/build/arm64/bin/app", "kind": "executable", "size": 123456}
    ],
    "primary_artifact": "/path/to/project/build/arm64/bin/app"
}
```

## 常见问题

### Q: 如何指定自定义交叉编译工具链？
A: 在 CMakePresets.json 中配置 toolchainFile，或使用 --toolchain 参数指定。

### Q: 如何设置环境变量？
A: 在构建前设置环境变量，如：
```bash
export CROSS_COMPILE=aarch64-linux-gnu-
export PATH=/opt/toolchain/bin:$PATH
```