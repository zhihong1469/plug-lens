# plug-lens：插镜感知终端
[简体中文](README.md) | [English](README.en.md)

🎬 **演示视频**
- V1_6ull系列：[B站5分钟链接](https://www.bilibili.com/video/BV1ZsVa6pEsY/)
- V2_rk3562系列：[10秒本地速览](docs/effect%20display/rk3562_quickly_look_2026-06-22_09-50-52.gif)


---

## 📛 项目名称由来
**插镜感知终端（plug-lens）**
「镜」指代摄像头视觉成像单元，也抽象为各类环境感知硬件；
「插」代表**可插拔模块化架构**，基于事件总线 + 数据总线双总线解耦设计，支持按需扩展摄像头、传感器等外设，业务模块可自由裁剪，复用迁移性极强。

---

## 📖 项目概述
plug-lens 是一个基于嵌入式 Linux 的**跨平台边缘视觉 AI 采集终端框架**，支持多硬件平台：

| 版本系列 | 硬件平台 | 核心特性 | 状态 |
|----------|----------|----------|------|
| **V1_6ull** | NXP i.MX6ULL (Cortex-A7@792MHz) | USB摄像头采集 + MNN人脸检测 + RTSP推流 | ✅ 稳定发布 |
| **V2_rk3562** | Rockchip RK3562 (4×A53@2.0GHz + 1TOPS NPU) | CSI摄像头 + RKNN/NPU加速 + MPP硬编码 | ⚡ 开发中 |

系统环境：Linux + 定制 Buildroot + sysvinit（摒弃 systemd）。定位为**资源极致受限**场景下的工业级边缘视觉解决方案。

### 核心能力
- **全局统一配置**：视频参数（分辨率、帧率、码率）全系统共用一套配置
- **软件降频策略**：各服务独立实现软件帧采样降频，适配不同硬件算力
- **双总线架构**：事件总线传输控制指令，数据总线路由视频裸流
- **插件化设计**：业务模块按需加载，编译期自动注册

---

## ✨ 核心特性

### 🏗️ 架构设计
- **五层分层架构**：应用层 → 总线层 → 业务服务层 → 基建层 → 基础层
- **双总线解耦**：事件总线（控制指令）+ 数据总线（视频帧/AI数据）
- **全局配置统一**：所有模块共用 `vision_ai_config.h` 中的视频基准宏
- **插件化体系**：基于 `initcall` 编译期自动注册

### 🎯 业务功能
| 服务 | 功能 | 软件降频策略 | 线程优先级 |
|------|------|-------------|-----------|
| **capture_srv** | USB/CSI摄像头采集 | 30FPS → 14FPS | 80 |
| **face_detect_srv** | MNN/RKNN人脸检测 | 14FPS → 5FPS | 70 |
| **net_push_srv** | RTSP流媒体推流 | 14FPS → 5FPS | 90 |
| **img_storage** | SD卡人脸抓拍存储 | 事件触发 | - |

### ⚙️ 软件降频机制
针对资源受限场景，采用**多级软件帧采样降频**策略：

```
摄像头硬件采集 (28FPS)
       ↓
   采集服务降频 (每2帧取1帧 → 14FPS)
       ↓                                    ↓
   人脸检测降频 (每14帧取1帧 → 1FPS)    推流服务降频 (每2帧取1帧 → 7FPS)

```

**设计优势**：
- 各服务独立控制帧率，避免级联性能瓶颈
- 低功耗唤醒机制，无事件时线程休眠
- RTSP客户端自适应，无客户端时自动降功耗

### 🛡️ 运行稳定性
- 线程隔离、事件低功耗唤醒，无空轮询
- 静态内存池 + 引用计数管理，**零内存泄漏**
- 完整资源回收机制，异常场景无僵尸进程

---

## 🔧 全局配置体系

### 统一视频基准宏（`vision_ai_config.h`）

```c
// 全局统一视频参数（所有模块共用）
#define GLOBAL_VIDEO_FPS           14      // 统一帧率
#define GLOBAL_VIDEO_WIDTH         640     // 统一宽度
#define GLOBAL_VIDEO_HEIGHT        360     // 统一高度
#define GLOBAL_JPEG_QUALITY        75      // JPEG压缩质量

// H.264编码配置
#define GLOBAL_H264_BITRATE_KBPS   500     // 码率
#define GLOBAL_H264_GOP            28      // I帧间隔
```

### 服务降频配置

| 服务 | 输入帧率 | 输出帧率 | 降频因子 | 代码位置 |
|------|---------|---------|---------|---------|
| capture_srv | 30FPS | 14FPS | 2 | `FPS_DOWNSAMPLE_STEP = CAPTURE_FPS / AI_TARGET_FPS` |
| face_detect_srv | 14FPS | 5FPS | 14 | `FPS_DOWNSAMPLE_STEP = 14` |
| net_push_srv | 14FPS | 5FPS | 2 | `FPS_DOWNSAMPLE_STEP = 2` |

---

## 🛠️ 硬件 & 运行环境

### 硬件平台

| 平台 | 主控 | 核心配置 | 外设支持 |
|------|------|----------|----------|
| **i.MX6ULL** | NXP Cortex-A7@792MHz | 512MB DDR3 | USB摄像头、LED、SD卡、以太网 |
| **RK3562** | Rockchip 4×A53@2.0GHz | 1GB LPDDR4 + 1TOPS NPU | CSI摄像头、LED、SD卡、以太网 |

### 软件环境
请看具体开发板的指南文档
- [快速上手（RK3562）](docs/quick_start_zh-CN_rk3562.md)

---

## 📊 性能指标

| 性能项 | i.MX6ULL | RK3562（软件模式） | RK3562（NPU模式） |
|--------|----------|-------------------|-------------------|
| 摄像头采集 | 28FPS | 28FPS | 30FPS |
| 人脸检测 | 1FPS | 1FPS | 30FPS+ |
| RTSP推流 | 7FPS | 7FPS | 14FPS+ |
| 内存占用 | < 100MB | < 150MB | < 200MB |

---

## 🚀 快速开始

### 编译命令

```bash
# i.MX6ULL 平台
use_toolchain arm32-linux-hf6ull
make TARGET_PLATFORM=imx6ull

# RK3562 平台（软件模式）
use_toolchain arm64-linux-75
make TARGET_PLATFORM=rk3562 ENGINE=software

# RK3562 平台（硬件模式，需厂商库）
use_toolchain arm64-linux-103
make TARGET_PLATFORM=rk3562 ENGINE=hardware
```

### 运行服务

```bash
# 启动所有服务
cd /root/run_on_board
./start_all.sh

# 查看服务状态
./status.sh

# 停止服务
./stop_all.sh
```

---

## 📚 文档导航
- [快速上手（RK3562）](docs/quick_start_zh-CN_rk3562.md)
- [快速上手（i.MX6ULL）](docs/quick_start_zh-CN_imx6ull.md)
- [架构说明](docs/architecture.md)
- [技能指南](SKILL.md)
- [性能报告](docs/performance_report.md)

---

## 🔀 分支与版本管理

| 分支名称 | 定位与用途 |
|----------|----------|
| `main` | 项目基线，仅在版本发布里程碑合并稳定代码 |
| `release/v1_6ull` | i.MX6ULL 平台稳定分支 |
| `release/v2_rk3562` | RK3562 平台开发分支 |
| `feature/*/*` | 临时开发分支 |
| `fix/*/*` | 临时缺陷修复分支 |

---

## 🧩 第三方开源依赖

| 开源项目 | 用途 | 开源协议 |
|----------|------|----------|
| MNN | 轻量化AI推理 | Apache 2.0 |
| RKNN | NPU加速推理 | 专有协议 |
| Live555 | RTSP流媒体服务 | LGPL 2.1 |
| libjpeg-turbo | JPEG编解码 | BSD-3-Clause |
| OpenH264 | H264软编码 | BSD 2-Clause |
| libyuv | YUV格式转换 | BSD-3-Clause |

---

## 📄 开源许可证
- 项目自研代码采用 **MIT License**
- 所有第三方组件遵循各自原厂开源协议

---

## 🙏 致谢
1. 感谢各开源项目开发者提供底层技术支撑；
2. 感谢韦东山团队i.MX6ULL驱动架构参考；
3. 感谢国内嵌入式开源社区的技术交流与经验分享。
