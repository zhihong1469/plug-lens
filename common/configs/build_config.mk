# ==========================================================================
# Build Configuration File
# @brief  Centralized configuration for build options
# @usage  Modify this file to select platform and engine options
# @note   Can be overridden by command-line arguments
# ==========================================================================

# ===================== Platform Selection =====================
# Available: imx6ull, rk3562
TARGET_PLATFORM ?= rk3562

# ===================== Engine Selection =====================
# Available: hardware, software
# - hardware: Use platform-specific hardware acceleration (RKNN/RGA/MPP)
# - software: Use pure software implementations (MNN/libyuv/openh264)
ENGINE ?= software

# ===================== Toolchain Auto-selection =====================
# Toolchain is auto-selected based on TARGET_PLATFORM and ENGINE
# - rk3562 + hardware  → arm64-linux-103 (vendor toolchain for hardware libs)
# - rk3562 + software  → arm64-linux-75  (generic toolchain)
# - imx6ull            → arm32-linux-hf6ull
