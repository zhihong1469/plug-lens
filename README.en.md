# plug-lens: Plug-in Perception Terminal
[简体中文](README.md) | [English](README.en.md)

🎬 **Demo Videos**
- V1_6ull Series: [Bilibili Link](https://www.bilibili.com/video/BV1ZsVa6pEsY/)
- V2_rk3562 Series: [10s Preview](docs/effect%20display/rk3562_quickly_look_2026-06-22_09-50-52.gif)

---

## 📛 Project Name Origin
**plug-lens**
- "Lens" refers to the camera visual imaging unit, and also abstracts into various environmental perception hardware;
- "Plug" represents the **pluggable modular architecture**, based on the decoupled design of Event Bus + Data Bus, supporting on-demand expansion of peripherals such as cameras and sensors. Business modules can be freely cropped with extremely strong reusability and portability.

---

## 📖 Project Overview
plug-lens is a **cross-platform edge vision AI acquisition terminal framework** based on embedded Linux, supporting multiple hardware platforms:

| Version Series | Hardware Platform | Core Features | Status |
|---------------|------------------|---------------|--------|
| **V1_6ull** | NXP i.MX6ULL (Cortex-A7@792MHz) | USB Camera + MNN Face Detection + RTSP Streaming | ✅ Stable Release |
| **V2_rk3562** | Rockchip RK3562 (4×A53@2.0GHz + 1TOPS NPU) | CSI Camera + RKNN/NPU Acceleration + MPP Hardware Encoding | ⚡ Under Development |

System Environment: Linux + Custom Buildroot + sysvinit (systemd abandoned). Positioned as an **industrial-grade edge vision solution for extremely resource-constrained scenarios**.

### Core Capabilities
- **Global Unified Configuration**: Video parameters (resolution, FPS, bitrate) shared across the entire system
- **Software Frame Rate Downsampling**: Each service independently implements software frame sampling to adapt to different hardware computing power
- **Dual-Bus Architecture**: Event Bus for control commands, Data Bus for video stream routing
- **Plugin Design**: Business modules loaded on demand with compile-time automatic registration

---

## ✨ Core Features

### 🏗️ Architecture Design
- **Five-Layer Architecture**: Application Layer → Bus Layer → Business Service Layer → Infrastructure Layer → Foundation Layer
- **Dual-Bus Decoupling**: Event Bus (control commands) + Data Bus (video frames/AI data)
- **Global Configuration Unification**: All modules share video reference macros from `vision_ai_config.h`
- **Plugin System**: Based on `initcall` compile-time automatic registration

### 🎯 Business Functions
| Service | Function | Software Downsampling Strategy | Thread Priority |
|---------|----------|--------------------------------|-----------------|
| **capture_srv** | USB/CSI Camera Capture | 30FPS → 14FPS | 80 |
| **face_detect_srv** | MNN/RKNN Face Detection | 14FPS → 5FPS | 70 |
| **net_push_srv** | RTSP Streaming | 14FPS → 5FPS | 90 |
| **img_storage** | SD Card Face Capture Storage | Event-triggered | - |

### ⚙️ Software Frame Rate Downsampling Mechanism
For resource-constrained scenarios, a **multi-stage software frame sampling downsampling** strategy is adopted:

```
Camera Hardware Capture (28FPS)
       ↓
   Capture Service Downsampling (1 in 2 frames → 14FPS)
       ↓                                    ↓
   Face Detection Downsampling (1 in 14 frames → 1FPS)    Streaming Downsampling (1 in 2 frames → 7FPS)
```

**Design Advantages**:
- Each service independently controls frame rate, avoiding cascading performance bottlenecks
- Low-power wake-up mechanism, threads sleep when no events
- RTSP client adaptive, automatic power reduction when no clients connected

### 🛡️ Operational Stability
- Thread isolation and event-based low-power wake-up, no empty polling
- Static memory pool + reference count management, **zero memory leaks**
- Complete resource recycling mechanism, no zombie processes in abnormal scenarios

---

## 🔧 Global Configuration System

### Unified Video Reference Macros (`vision_ai_config.h`)

```c
// Global unified video parameters (shared by all modules)
#define GLOBAL_VIDEO_FPS           14      // Unified frame rate
#define GLOBAL_VIDEO_WIDTH         640     // Unified width
#define GLOBAL_VIDEO_HEIGHT        360     // Unified height
#define GLOBAL_JPEG_QUALITY        75      // JPEG compression quality

// H.264 encoding configuration
#define GLOBAL_H264_BITRATE_KBPS   500     // Bitrate in kbps
#define GLOBAL_H264_GOP            28      // I-frame interval
```

### Service Downsampling Configuration

| Service | Input FPS | Output FPS | Downsampling Factor | Code Location |
|---------|-----------|------------|---------------------|---------------|
| capture_srv | 30FPS | 14FPS | 2 | `FPS_DOWNSAMPLE_STEP = CAPTURE_FPS / AI_TARGET_FPS` |
| face_detect_srv | 14FPS | 5FPS | 14 | `FPS_DOWNSAMPLE_STEP = 14` |
| net_push_srv | 14FPS | 5FPS | 2 | `FPS_DOWNSAMPLE_STEP = 2` |

---

## 🛠️ Hardware & Runtime Environment

### Hardware Platform

| Platform | Main Controller | Core Configuration | Peripherals |
|----------|----------------|-------------------|-------------|
| **i.MX6ULL** | NXP Cortex-A7@792MHz | 512MB DDR3 | USB Camera, LED, SD Card, Ethernet |
| **RK3562** | Rockchip 4×A53@2.0GHz | 1GB LPDDR4 + 1TOPS NPU | CSI Camera, LED, SD Card, Ethernet |

### Software Environment
Please refer to the specific development board guide:
- [Quick Start (RK3562)](docs/quick_start_zh-CN_rk3562.md)

---

## 📊 Performance Metrics

| Performance Item | i.MX6ULL | RK3562 (Software Mode) | RK3562 (NPU Mode) |
|------------------|----------|------------------------|-------------------|
| Camera Capture | 28FPS | 28FPS | 30FPS |
| Face Detection | 1FPS | 1FPS | 30FPS+ |
| RTSP Streaming | 7FPS | 7FPS | 14FPS+ |
| Memory Usage | < 100MB | < 150MB | < 200MB |

---

## 🚀 Quick Start

### Compilation Commands

```bash
# i.MX6ULL Platform
use_toolchain arm32-linux-hf6ull
make TARGET_PLATFORM=imx6ull

# RK3562 Platform (Software Mode)
use_toolchain arm64-linux-75
make TARGET_PLATFORM=rk3562 ENGINE=software

# RK3562 Platform (Hardware Mode, requires vendor libraries)
use_toolchain arm64-linux-103
make TARGET_PLATFORM=rk3562 ENGINE=hardware
```

### Running Services

```bash
# Start all services
cd /root/run_on_board
./start_all.sh

# Check service status
./status.sh

# Stop services
./stop_all.sh
```

---

## 📚 Documentation Navigation
- [Quick Start (RK3562)](docs/quick_start_zh-CN_rk3562.md)
- [Quick Start (i.MX6ULL)](docs/quick_start_zh-CN_imx6ull.md)
- [Architecture Description](docs/architecture.md)
- [Skill Guide](SKILL.md)
- [Performance Report](docs/performance_report.md)
- [Contributing](docs/CONTRIBUTING.en.md)

---

## 🔀 Branch and Version Management

| Branch Name | Positioning and Purpose |
|-------------|-------------------------|
| `main` | Project baseline, merges stable code only at release milestones |
| `release/v1_6ull` | i.MX6ULL platform stable branch |
| `release/v2_rk3562` | RK3562 platform development branch |
| `feature/*/*` | Temporary development branches |
| `fix/*/*` | Temporary bug fix branches |

---

## 🧩 Third-Party Open Source Dependencies

| Open Source Project | Purpose | Open Source License |
|---------------------|---------|---------------------|
| MNN | Lightweight AI inference | Apache 2.0 |
| RKNN | NPU-accelerated inference | Proprietary |
| Live555 | RTSP streaming media service | LGPL 2.1 |
| libjpeg-turbo | JPEG codec | BSD-3-Clause |
| OpenH264 | H264 software encoding | BSD 2-Clause |
| libyuv | YUV format conversion | BSD-3-Clause |

---

## 📄 Open Source License
- Project self-developed code adopts **MIT License**
- All third-party components follow their respective original open source licenses

---

## 🙏 Acknowledgments
1. Thanks to the developers of various open source projects for providing underlying technical support;
2. Thanks to Wei Dongshan team for the i.MX6ULL driver architecture reference;
3. Thanks to the domestic embedded open source community for technical exchanges and experience sharing.
