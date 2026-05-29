# plug-lens：插镜感知终端
**v1.0.0 | 发布日期：2026-05-29**

[English](README.md) | [简体中文](README_zh-CN.md)

🎬 **项目演示B站视频**：https://www.bilibili.com/video/BV1ZsVa6pEsY/

---

## 📛 项目名称由来
**插镜感知终端（plug-lens）**
「镜」指代摄像头视觉成像单元，也抽象为各类环境感知硬件；
「插」代表**可插拔模块化架构**，基于事件总线 + 数据总线双总线解耦设计，支持按需扩展摄像头、传感器等外设，业务模块可自由裁剪、复用迁移性极强。

---

## 📖 项目概述
plug-lens 是基于 **NXP i.MX6ULL（Cortex-A7 792MHz）** 打造的**工业级嵌入式Linux视觉AI采集终端**。
运行环境：Linux 4.9.88 + 裁剪版 Buildroot + sysvinit（无 systemd）。

### 核心能力
集成 **USB摄像头采集 + MNN离线人脸检测 + SD卡人脸抓拍 + RTSP多客户端实时推流** 全链路业务；
采用**C语言面向对象、四层分层架构、双总线通信、编译期自动初始化**工业级工程设计，低功耗、高稳定、易扩展，可直接落地边缘视觉监控、无人值守抓拍、实时流媒体传输等场景。

> 备注：代码内置状态机、自定义协议框架，v1.0.0 版本暂未启用，预留后续扩展。

---

## ✨ 核心特性
### 🏗️ 架构设计
- 四层分层架构：应用编排层 / 业务服务层 / 中间枢纽层 / 系统基建层，层间单向依赖
- 双总线解耦：**事件总线**传控制指令、**数据总线**路由视频裸流，完全解耦业务模块
- 插件化体系：基于 `initcall` 编译期自动注册，模块按需加载、独立编译
- 抽象基类封装：硬件与业务逻辑解耦，便于跨平台快速移植

### 🎯 业务功能
- USB YUYV 原始视频帧采集
- UltraLight 轻量化模型 + MNN 离线人脸检测（无云端依赖）
- 人脸触发自动抓拍，SD 卡循环存储
- Live555 实现 RTSP 流媒体服务，多设备同时拉流
- LED 状态指示灯联动工作状态

### ⚙️ 工业级工程规范
- 完整顶层 Makefile + 自定义链接脚本，标准交叉编译工程
- 静态内存池 + 引用计数管理视频帧生命周期，**零内存泄漏**
- 单职责多线程设计，实时优先级调度，适配嵌入式低功耗
- 分级日志系统，方便现场调试与运维排查
- 守护进程保活、一键启停脚本、开机自启整套运维方案

### 🛡️ 运行稳定性
- 线程隔离、事件低功耗唤醒，无空轮询 CPU 占用
- 帧采样降频控制，适配 i.MX6ULL 算力瓶颈
- RTSP 客户端状态自适应，无人流自动休眠降功耗
- 完整资源回收机制，异常不僵尸、不内存泄漏

---

## 🛠️ 硬件 & 运行环境
### 硬件平台
- 主控：100ask NXP i.MX6ULL（Cortex-A7 792MHz）
- 外设：USB 摄像头、板载 LED、SD 存储卡

### 软件环境
- 系统：Linux 4.9.88 + 裁剪 Buildroot
- 初始化：sysvinit（无 systemd）
- 编译：ARM32 嵌入式交叉编译工具链
- 板端固定工作目录：`/root/run_on_board`

### 网络支持
支持双模式切换：
1. **直连PC**：纯开发调试
2. **路由器局域网**：多设备同网段 RTSP 拉流

---

## 🏗️ 系统架构
### 分层架构
应用编排层 → 业务服务层 → 中间枢纽层 → 系统基建层
内核驱动层完全独立隔离，不耦合应用业务。

### 核心数据流
摄像头驱动采集 → 业务帧服务 → 数据总线分发 → 人脸检测 / RTSP推流 / 图片存储 → 自动帧回收

---

## 📊 性能指标
| 性能项 | 指标值 | 说明 |
|--------|--------|------|
| 视频分辨率 | 640 × 360 | 摄像头固定输出 |
| RTSP 推流帧率 | 7fps | 局域网稳定传输 |
| 人脸检测帧率 | 1fps | MNN 离线推理 |
| 内存占用 | < 100MB | 虚拟内存预留，实际占用更低 |

---

## 🚀 快速开始
完整流程：
PC 交叉编译 → NFS 文件分发 → 开发板网络挂载 → 加载 LED 驱动 → 脚本启停 → RTSP 拉流播放

详细编译、NFS 配置、部署运行、调试排错步骤，请查阅：
[快速上手指南](docs/quick_start_zh-CN.md)

---

## 📚 文档导航
- [快速上手](docs/quick_start_zh-CN.md)：编译/部署/运行/调试全流程
- 其他运维、接口文档后续持续补充完善

---

## 🧩 第三方开源依赖
| 开源项目 | 用途 | 许可证 | 仓库地址 |
|--------|------|--------|----------|
| MNN | 阿里轻量化AI推理框架 | Apache 2.0 | https://github.com/alibaba/MNN |
| Ultra-Light-Fast-Generic-Face-Detector-1MB | 轻量化人脸检测模型 | MIT | https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB |
| Live555 | RTSP/ RTP 流媒体库 | LGPL 2.1 | http://www.live555.com/liveMedia/ |
| libjpeg-turbo | JPEG 编解码加速 | BSD-3-Clause | https://github.com/libjpeg-turbo/libjpeg-turbo |
| OpenH264 | H.264 视频编解码 | BSD 2-Clause | https://github.com/cisco/openh264 |
| libyuv | YUV 色彩空间转换 | BSD-3-Clause | https://github.com/lemenkov/libyuv |
| OpenCV | 图像绘制调试 | Apache 2.0 | https://github.com/opencv/opencv |
| GDB 12.1 | 嵌入式远程调试 | GPL-3.0 | https://ftp.gnu.org/gnu/gdb/gdb-12.1.tar.xz |

### 参考项目
- 100askTeam：Linux 驱动分层架构参考
- 兆鸣嵌入式：C 语言面向对象工程思想参考
- FFmpeg-Builds：PC 端音视频测试工具

---

## 📄 开源许可证
- 本项目**plug-lens 自有代码**采用 **MIT License** 开源
- 所有第三方库遵循各自原始开源协议
- 第三方许可证文件存放于 `third_lib/` 对应目录

---

## 🙏 致谢
1. 感谢所有上述优秀开源项目开发者，为本项目提供底层技术支撑；
2. 感谢韦东山团队提供 i.MX6ULL 驱动架构参考；
3. 感谢嵌入式开源社区技术交流与经验分享。

---

🎬 项目演示回看：
https://www.bilibili.com/video/BV1ZsVa6pEsY/