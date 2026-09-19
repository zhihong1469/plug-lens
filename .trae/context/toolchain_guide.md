# Plug-Lens Toolchain and Build Configuration Guide

## Overview

This document describes the toolchain selection logic and build configuration for the Plug-Lens project, designed for cross-compilation targeting the RK3562 development board.

---

## 1. Toolchain Selection Logic

### 1.1 Available Toolchains

| Toolchain ID | Platform | Toolchain | Description |
|--------------|----------|-----------|-------------|
| `arm64-linux-103` | RK3562 | GCC 10.3 | Musl/none ABI, for hardware acceleration |
| `arm64-linux-75` | RK3562 | GCC 7.5 | glibc ABI, for software mode |
| `arm32-linux-hf6ull` | i.MX6ULL | Buildroot | Hard-float glibc |

### 1.2 Toolchain Selection Flow

```
TARGET_PLATFORM + ENGINE → Toolchain Selection

RK3562 + hardware → arm64-linux-103 (GCC 10.3)
RK3562 + software → arm64-linux-75 (GCC 7.5)
i.MX6ULL + * → arm32-linux-hf6ull
```

### 1.3 Manual Toolchain Activation (WSL2)

```bash
# Activate RK3562 hardware toolchain (GCC 10.3)
use_toolchain arm64-linux-103

# Environment variables set:
#   ARCH=arm64
#   CROSS_COMPILE=aarch64-none-linux-gnu-
#   PATH=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin:$PATH
```

---

## 2. Build Configuration

### 2.1 Configuration File

**File:** `common/configs/build_config.mk`

```makefile
# Platform Selection (rk3562 or imx6ull)
TARGET_PLATFORM ?= rk3562

# Engine Selection (hardware or software)
# - hardware: RKNN/RGA/MPP hardware acceleration
# - software: MNN/libyuv/openh264 software implementation
ENGINE ?= hardware
```

### 2.2 Command-Line Overrides

```bash
# Build RK3562 with hardware acceleration
make TARGET_PLATFORM=rk3562 ENGINE=hardware

# Build RK3562 with software mode
make TARGET_PLATFORM=rk3562 ENGINE=software

# Build for i.MX6ULL
make TARGET_PLATFORM=imx6ull
```

### 2.3 Platform/Engine Macro Mapping

When building, the following macros are passed to the compiler via `-D` flags:

| Make Variable | Compiler Macro | Hardware Mode | Software Mode |
|---------------|---------------|---------------|---------------|
| PLATFORM_RK3562 | PLATFORM_RK3562 | 1 | 1 |
| PLATFORM_IMX6ULL | PLATFORM_IMX6ULL | 0 | 0 |
| AI_ENGINE_RKNN | AI_ENGINE_RKNN | 1 | 0 |
| AI_ENGINE_MNN | AI_ENGINE_MNN | 0 | 1 |
| IMG_PROC_RGA | IMG_PROC_RGA | 1 | 0 |
| VIDEO_ENCODER_MPP | VIDEO_ENCODER_MPP | 1 | 0 |

### 2.4 Build Process

```bash
# Step 1: Activate toolchain (if not already done)
use_toolchain arm64-linux-103

# Step 2: Clean and build
cd /home/luo/linux/project/plug-lens
make clean
make

# Step 3: Output
# Executable: output/vision_ai_app
# Libraries: output/libplug.a, output/libcom.a
```

---

## 3. Third-Party Library Configuration

### 3.1 Library Mapping by Mode

**Hardware Mode (RK3562):**
- AI: RKNN (`third_lib/rknn/lib_rk3562`)
- Image: RGA (`third_lib/rkrga/lib_rk3562`)
- Video: MPP (`third_lib/rkmpp/lib_rk3562`)
- JPEG: libjpeg-turbo (`third_lib/libjpeg_turbo/lib_rk3562`)
- LIVE555: (`third_lib/live555/lib_rk3562`)

**Software Mode (RK3562):**
- AI: MNN (`third_lib/mnn/lib_aarch64`)
- Image: libyuv (`third_lib/libyuv/lib_aarch64`)
- Video: openh264 (`third_lib/openh264/lib_aarch64`)
- JPEG: libjpeg-turbo (`third_lib/libjpeg_turbo/lib_aarch64`)
- LIVE555: (`third_lib/live555/lib_aarch64`)

### 3.2 Library Paths

```
third_lib/
├── rknn/                    # RKNN NPU runtime
│   ├── include/             # rknn_api.h, rknn_custom_op.h, rknn_matmul_api.h
│   └── lib_rk3562/         # librknnrt.so, libmk_api.so, librga.so
├── rkrga/                   # RGA hardware image processing
│   ├── include/             # rga.h
│   └── lib_rk3562/         # librga.so
├── rkmpp/                   # MPP video encoding
│   ├── include/             # mpp_buffer.h, mpp_frame.h, etc.
│   └── lib_rk3562/         # librockchip_mpp.so
├── mnn/                     # MNN CPU inference (software fallback)
│   ├── include/             # MNN headers
│   ├── lib_aarch64/         # libMNN.so (software mode)
│   └── lib_rk3562/          # libMNN.so (hardware mode)
├── libjpeg_turbo/           # JPEG codec
├── libyuv/                  # YUV format conversion
├── live555/                 # RTSP streaming
└── openh264/                # H.264 software encoding
```

---

## 4. Plugin Selection

### 4.1 AI Model Plugins

**RKNN Mode (AI_ENGINE_RKNN=1):**
```
plugins/base_plugins/ai_model_rknn/
├── inc/ai_model_rknn.hpp    # Public API header
└── src/ai_model_rknn.cpp    # RKNN implementation
```

**MNN Mode (AI_ENGINE_MNN=1):**
```
plugins/base_plugins/ai_model_mnn/
├── inc/ai_model_mnn.hpp     # Public API header
├── inc/UltraFaceMNN.hpp     # UltraFace MNN wrapper
├── src/ai_model_mnn.cpp     # MNN implementation
└── src/UltraFaceMNN.cpp     # UltraFace implementation
```

### 4.2 Image Processing Plugins

**RGA Mode (IMG_PROC_RGA=1):**
```
plugins/base_plugins/img_rga/
├── inc/img_rga.h            # RGA API header
└── src/img_rga.cpp          # RGA hardware implementation
plugins/base_plugins/img_joint/
├── inc/img_joint.h          # Unified image processing API
├── src/img_joint.cpp        # Joint implementation (for encoding)
└── src/img_proc_software_ops.cpp  # Software encoding fallback
```

**Software Mode (IMG_PROC_SOFTWARE=1):**
```
plugins/base_plugins/img_joint/
├── inc/img_joint.h          # Unified image processing API
├── src/img_joint.cpp        # Joint implementation
└── src/img_proc_software_ops.cpp  # Software implementation
```

**Note:** In RGA mode, conversion/resize uses RGA hardware, but H.264 encoding currently uses openh264 software. MPP hardware encoding is configured in `board_option.h` but not yet implemented in code.

---

## 5. Development Board Deployment

### 5.1 Deployment Steps

```bash
# 1. Copy executable to board
scp output/vision_ai_app root@192.168.5.11:/tmp/

# 2. Copy model file
scp third_lib/face_detector/model_rk/RK3562/face_detector.rknn root@192.168.5.11:/tmp/

# 3. Copy all dependency libraries
scp third_lib/rknn/lib_rk3562/*.so* root@192.168.5.11:/tmp/lib/
scp third_lib/rkrga/lib_rk3562/*.so* root@192.168.5.11:/tmp/lib/
scp third_lib/rkmpp/lib_rk3562/*.so* root@192.168.5.11:/tmp/lib/
scp third_lib/libjpeg_turbo/lib_rk3562/*.so* root@192.168.5.11:/tmp/lib/
scp third_lib/libyuv/lib_aarch64/*.so* root@192.168.5.11:/tmp/lib/
scp third_lib/openh264/lib_aarch64/*.so* root@192.168.5.11:/tmp/lib/
scp third_lib/mnn/lib_rk3562/*.so* root@192.168.5.11:/tmp/lib/

# 4. Run on board
ssh root@192.168.5.11 'cd /tmp && export LD_LIBRARY_PATH=/tmp/lib:$LD_LIBRARY_PATH && ./vision_ai_app'
```

### 5.2 Runtime Environment

**Required Environment Variables:**
```bash
export LD_LIBRARY_PATH=/tmp/lib:$LD_LIBRARY_PATH
```

**Required Device Nodes:**
- `/dev/dri/card1` - RKNN NPU (DRM device)
- `/dev/rga` - RGA image processing
- `/dev/mpp_service` - MPP video encoding
- `/dev/video*` - V4L2 camera
- `/dev/mali0` - Mali GPU (optional)

---

## 6. Common Build Issues and Solutions

### 6.1 Toolchain Path Mismatch (Critical)

**Error:**
```
make: /usr/local/arm/gcc-linaro-10.3.1-2021.07-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc: No such file or directory
```

**Root Cause:**
Makefile 中的工具链路径与实际安装路径不匹配。

**Solution:**
编辑 [Makefile](file:///home/luo/linux/project/plug-lens/Makefile) 第 63-64 行，修正工具链路径：

```makefile
# 修正前（错误路径）
TOOLCHAIN_PATH := /usr/local/arm/gcc-linaro-10.3.1-2021.07-x86_64_aarch64-linux-gnu/bin
CROSS_COMPILE := $(TOOLCHAIN_PATH)/aarch64-linux-gnu-

# 修正后（正确路径）
TOOLCHAIN_PATH := /usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin
CROSS_COMPILE := $(TOOLCHAIN_PATH)/aarch64-none-linux-gnu-
```

**验证:**
```bash
ls /usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc
# 输出: /usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc
```

### 6.2 Toolchain Not Found

**Error:**
```
bash: use_toolchain: command not found
```

**Solution:**
```bash
# Ensure ~/.bashrc is sourced
source ~/.bashrc
use_toolchain arm64-linux-103
```

### 6.3 Missing Third-Party Libraries

**Error:**
```
error while loading shared libraries: libturbojpeg.so.0: cannot open shared object file
```

**Solution:**
Ensure all required libraries are copied to the target board.

### 6.4 AI Model Initialization Failed

**Error:**
```
[FACE_DETECT]AI model initialization failed
```

**Checklist:**
1. Verify model path in `CONFIG_AI_MODEL_PATH`
2. Verify model file exists on target
3. Verify `AI_ENGINE_RKNN` is set to 1
4. Verify `librknnrt.so` is in `LD_LIBRARY_PATH`
5. Verify `/dev/dri/card1` exists on target

### 6.5 Undefined Reference to ai_model_rknn_ops

**Error:**
```
undefined reference to `ai_model_rknn_ops'
```

**Solution:**
Ensure `AI_ENGINE_RKNN=1` is passed to both Makefile and plugins/Makefile.

### 6.6 MPP Bitrate Unit Mismatch (Critical)

**Error:**
```
mpp_enc: invalid bit per second (bps) 500 [250:1000] out of range 1K~100M
mpp_enc: restore bps to 2000000 [1500000:2500000]
```

**Root Cause:**
`h264_enc_config_t.bitrate` 的单位是 **kbps**（千比特/秒），但 `img_rga.cpp` 直接将其作为 **bps**（比特/秒）传递给 MPP，导致 500 kbps 变成了 500 bps。

**Solution:**
编辑 [img_rga.cpp](file:///home/luo/linux/project/plug-lens/plugins/base_plugins/img_rga/src/img_rga.cpp) 第 398 行，添加单位转换：

```cpp
// 修复前
enc_ctx->bitrate = cfg->bitrate > 0 ? cfg->bitrate : 2000000;

// 修复后
enc_ctx->bitrate = cfg->bitrate > 0 ? cfg->bitrate * 1000 : 2000000;
```

**验证:**
修复前：`invalid bit per second (bps) 500` → MPP 自动恢复为 2000000 bps
修复后：`bps [500000:1000000:250000]` → 正确的 500 kbps 设置

---

## 7. Configuration Summary Table

| Configuration | Hardware Mode | Software Mode |
|--------------|---------------|---------------|
| TARGET_PLATFORM | rk3562 | rk3562 |
| ENGINE | hardware | software |
| AI_ENGINE_RKNN | 1 | 0 |
| AI_ENGINE_MNN | 0 | 1 |
| IMG_PROC_RGA | 1 | 0 |
| VIDEO_ENCODER_MPP | 1 | 0 |
| Toolchain | arm64-linux-103 | arm64-linux-75 |
| AI Plugin | ai_model_rknn | ai_model_mnn |
| Image Plugin | img_rga + img_joint | img_joint |
| Video Encoder | MPP | openh264 |
| Model Format | .rknn | .mnn |

---

## 8. File References

| File | Purpose |
|------|---------|
| `common/configs/build_config.mk` | Build configuration entry point |
| `common/configs/board_option.h` | Platform/engine macros |
| `common/configs/vision_ai_config.h` | Application runtime configuration |
| `Makefile` | Top-level build script |
| `Makefile.build` | Common compilation rules |
| `plugins/Makefile` | Plugin selection logic |
| `third_lib/third_lib.mk` | Third-party library configuration |

---

## 9. Quick Reference

```bash
# Full build command for RK3562 hardware mode
use_toolchain arm64-linux-103 && cd /home/luo/linux/project/plug-lens && make clean && make

# Deploy and run
scp output/vision_ai_app third_lib/face_detector/model_rk/RK3562/face_detector.rknn root@192.168.5.11:/tmp/
ssh root@192.168.5.11 'cd /tmp && export LD_LIBRARY_PATH=/tmp/lib:$LD_LIBRARY_PATH && ./vision_ai_app'
```