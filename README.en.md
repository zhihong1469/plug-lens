# plug-lens: Plug-in Perception Terminal
[简体中文](README.md) | [English](README.en.md)
🎬 Project V1_6ull Series Demo Video: [Bilibili Link](https://www.bilibili.com/video/BV1ZsVa6pEsY/)

---

## 📛 Project Name Origin
**plug-lens (插镜感知终端)**
- "Lens" refers to the camera visual imaging unit, and also abstracts into various environmental perception hardware;
- "Plug" represents the **pluggable modular architecture**, based on the decoupled design of Event Bus + Data Bus, supporting on-demand expansion of peripherals such as cameras and sensors. Business modules can be freely cropped with extremely strong reusability and portability.

---

## 📖 Project Overview
The plug-lens V1_6ull series is an industrial-grade embedded Linux edge vision AI acquisition terminal developed based on **NXP i.MX6ULL (Cortex-A7@792MHz)**. System environment: Linux 4.9.88 + Custom Buildroot + sysvinit (systemd abandoned). Positioned as an **engineering thinking learning project under extremely resource-constrained scenarios**.

### Core Capabilities
Integrates the full-link business of **USB camera acquisition + MNN offline face detection + SD card face capture + RTSP multi-client real-time streaming**;
Adopts industrial-grade engineering design including **C language object-oriented programming, four-layer architecture, dual-bus communication, and compile-time automatic initialization**. With low power consumption, high stability and easy scalability, it can be directly applied to edge visual monitoring, unattended capture, real-time streaming media transmission and other scenarios.

> Note: The code has built-in state machine and custom protocol framework, which are not enabled in v1.x versions and reserved for future expansion.

---

## ✨ Core Features
### 🏗️ Architecture Design
- Four-layer architecture: Application Orchestration Layer / Business Service Layer / Middle Hub Layer / System Infrastructure Layer, with unidirectional dependency between layers
- Dual-bus decoupling: **Event Bus** transmits control commands, **Data Bus** routes raw video streams, achieving complete decoupling of business modules
- Plugin system: Based on `initcall` compile-time automatic registration, modules are loaded on demand and compiled independently
- Abstract base class encapsulation: Decouples different hardware devices from business logic, facilitating rapid cross-platform porting

### 🎯 Business Functions
- USB YUYV raw video frame acquisition
- UltraLight lightweight model + MNN offline face detection (no cloud dependency)
- Face-triggered automatic capture with SD card cyclic storage
- RTSP streaming media service implemented with Live555, supporting simultaneous streaming from multiple devices
- LED status indicator linked to hardware working state

### ⚙️ Industrial-Grade Engineering Specifications
- Complete top-level Makefile + custom linker script, standard cross-compilation project
- Static memory pool + reference count management for video frame lifecycle, **zero memory leaks**
- Single-responsibility multi-thread design with real-time priority scheduling, adapting to embedded low-power requirements
- Hierarchical logging system, facilitating on-site debugging and operation and maintenance troubleshooting
- Complete operation and maintenance solution including daemon process keep-alive, one-click start/stop script, and auto-start on boot

### 🛡️ Operational Stability
- Thread isolation and event-based low-power wake-up, no invalid CPU occupation caused by empty polling
- Frame sampling frequency reduction control, adapting to i.MX6ULL computing power bottleneck
- RTSP client state self-adaptation, automatic sleep to reduce power consumption when no client is streaming
- Complete resource recycling mechanism, no zombie processes or memory leaks in abnormal scenarios

---

## 🛠️ Hardware & Runtime Environment
### Hardware Platform
- Main Controller: 100ASK NXP i.MX6ULL (Cortex-A7@792MHz)
- Peripherals: USB camera, on-board LED, SD memory card, Ethernet port

### Software Environment
- System: Linux 4.9.88 + Tailored Buildroot
- Initialization: sysvinit (no systemd)
- Compilation: ARM32 embedded cross-compilation toolchain
- Fixed working directory on board: `/root/run_on_board`

### Network Support
Supports dual-mode switching:
1. **Direct connection to PC**: Pure development and debugging scenario
2. **Router LAN**: Multi-device RTSP streaming in the same network segment

---

## 🏗️ System Architecture
### Layered Architecture
Application Orchestration Layer → Business Service Layer → Middle Hub Layer → System Infrastructure Layer
The kernel driver layer is completely independent and isolated, and does not couple with upper-layer application business.

### Core Data Flow
Camera Driver Acquisition → Business Frame Service → Data Bus Distribution → Face Detection / RTSP Streaming / Image Storage → Automatic Frame Recycling

---

## 📊 Performance Metrics
| Performance Item | Value | Description |
|------------------|-------|-------------|
| Video Resolution | 640 × 360 | Fixed camera output specification |
| RTSP Streaming Frame Rate | 7fps | Stable transmission in LAN environment |
| Face Detection Frame Rate | 1fps | MNN local offline inference |
| Memory Usage | < 100MB | Virtual memory statistics, actual physical usage is lower |

---

## 🚀 Quick Start
Compilation and deployment process: PC cross-compilation → NFS mount distribution → Development board mount → Load driver/environment → Script start/stop → RTSP streaming verification
Detailed compilation, deployment and debugging tutorial: [Quick Start Guide](docs/quick_start_zh-CN.md)

## 📚 Documentation Navigation
- [Quick Start](docs/quick_start_zh-CN.md): Full process of compilation, deployment and operation
- [Architecture Description](docs/architecture.md): Project architecture block diagram and design ideas
- [Agent Scheduling](SKILL.md): AI programming scheduling example (not used yet due to account restrictions)
- [Performance Report](docs/performance_report.md): Detailed project performance evolution report based on actual logs and running data
- [Contributing](docs/CONTRIBUTING.en.md): If you have optimization ideas, welcome to participate in project co-construction
> [!TIP]
> Operation and maintenance manual, interface documentation, and engineering-specific architecture agent prompts are continuously iterated and supplemented at: [docs/waitme.md](docs/waitme.md)
> —— What?
> With this project framework, users can develop their own applications at extremely low learning cost in the AI era. Building an embedded community together is what I hope to do.
> —— Why?
> Although AI can complete 70% of basic code writing, the stability of project implementation still requires hardware testing and verification, and product launch requires human responsibility. A large amount of reusable engineering code cannot be separated from actual testing and polishing. Open source sharing allows developers to focus on higher-level technical learning; only with collective collaboration can the path of technical growth go further.
> —— How?
> The project community is still under preparation. Welcome to click the ⭐Star in the upper right corner of the page to support; the project will continue to iterate and optimize following industry trends, and I look forward to providing inspiration for your embedded development.

## 🔀 Branch and Version Management Specifications
This project follows enterprise-level Git R&D workflow and uses semantic versioning (SemVer `vX.Y.Z`) for iteration management:
### 1. Branch Responsibility Definition
| Branch Name | Positioning and Purpose |
|-------------|-------------------------|
| `main` | **Direct code submission is restricted**; project baseline (displays the latest stable version externally), only merges stable code from `release/*` branches at version release milestones; future `release/*` new hardware adaptation branches are derived from this branch |
| `release/*` | **Direct code submission is restricted**; permanently locked release stable branch for corresponding fixed hardware; all bug fixes, document additions, and function iterations are implemented here; all series version tags are generated based on this branch |
| `release/v1_6ull` | Permanently locked i.MX6ULL platform; single-core 792MHz + no NPU + no hardware-encoded GPU, developed based on resource extreme squeezing idea; implemented: `v4l2` acquisition, face detection + MJPEG encoding storage, H264 soft encoding + RTSP streaming, 7FPS is the current short-term performance limit of this hardware |
| `release/v2_rk3562` (Under construction) | Permanently locked rk3562 platform; 4×A53@2.0GHz + 1TOPS computing power + Mali-G52 2EE |
| `feature/*/*` | Temporary development branch for new function development and large-scale document supplementation. After development is completed, merge into release via PR with squash, and delete immediately after merging |
| `fix/*/*` | Temporary defect branch for bug fixes. After development is completed, merge into release via PR with squash, and delete immediately after merging |

### 2. Standardized Development Process
1. Create a feature/fix temporary branch from `release/v1_6ull`;
2. Local debugging and iteration, all commits use GPG signature: `git commit -S`;
3. Push to remote and create a PR, select `Squash and merge` to compress multiple scattered commits into a single standard record and merge into release;
4. After PR merging is completed, clean up local + remote temporary branches;
5. After the version is stable, create a GPG-signed Tag on the release branch and synchronize it to the main baseline.

### 3. Semantic Versioning `vX.Y.Z`
- X (Major version): Architecture reconstruction / hardware platform replacement (e.g., v2.0.0 adapts to RK series)
- Y (Minor version): New complete business functions, large-scale document reconstruction (e.g., v1.1.0 adds storage expansion module)
- Z (Patch version): Bug fixes, scattered document supplements (e.g., v1.0.1 improves the quick start document)

### 4. Version Release Rules (Key: Tag corresponds to official Release)
1. **Git Tag = Official project release snapshot**, only generate GPG-signed tags after full testing and document completion;
2. GitHub Release is bound to the corresponding Tag for uploading precompiled firmware and source code packages, serving as the official external release entry;
3. v1.0.0 is the first official release of the project, and the Release baseline has been anchored; subsequent v1.0.x patch versions will create new Tag + Release after iteration is completed.

## 🧩 Third-Party Open Source Dependencies

| Open Source Project | Purpose | Open Source License | Project Address |
|---------------------|---------|---------------------|-----------------|
| MNN | Lightweight AI inference | Apache 2.0 | https://github.com/alibaba/MNN |
| Ultra-Light-Fast-Generic-Face-Detector-1MB | Face detection model | MIT | https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB |
| Live555 | RTSP streaming media service | LGPL 2.1 | http://www.live555.com/liveMedia/ |
| libjpeg-turbo | JPEG image codec acceleration | BSD-3-Clause | https://github.com/libjpeg-turbo/libjpeg-turbo |
| OpenH264 | H264 soft encoding | BSD 2-Clause | https://github.com/cisco/openh264 |
| libyuv | YUV image format conversion | BSD-3-Clause | https://github.com/lemenkov/libyuv |
| OpenCV | Image debugging and drawing (debug dependency) | Apache 2.0 | https://github.com/opencv/opencv |
| GDB12.1 | Remote cross debugging (debug dependency) | GPL-3.0 | https://ftp.gnu.org/gnu/gdb/gdb-12.1.tar.xz |
> Marked "debug dependency" is only used for PC compilation and debugging, and is not packaged and integrated into the official firmware

### Reference Learning Projects
1. Wei Dongshan 100askTeam: Linux driver layered architecture reference
2. Zhaoming Embedded: C language object-oriented engineering design ideas
3. FFmpeg-Builds: PC-side audio and video debugging tool reference

## 📄 Open Source License
- Project self-developed code adopts **MIT License**, agreement file: `./LICENSE`
- All third-party components follow their respective original open source agreements, and the agreement documents are stored in the corresponding `third_lib/*` directory

## 🙏 Acknowledgments
1. Thanks to the developers of various open source projects for providing underlying technical support;
2. Thanks to Wei Dongshan team for the i.MX6ULL driver architecture reference;
3. Thanks to the domestic embedded open source community for technical exchanges and experience sharing.
