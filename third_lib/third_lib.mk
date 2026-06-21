# ==========================================================================
# Third-party Library Configuration
# @brief  Centralized configuration for all third-party libraries
# @usage  Include this file in Makefiles that need third-party library access
# ==========================================================================

# ------------------------------ Library Root Path ------------------------------
THIRD_LIB_ROOT := $(TOPDIR)/third_lib

# ------------------------------ Common Libraries (Shared Headers) ------------------------------
# Format: LIB_<NAME>_INC, LIB_<NAME>_LIB_<PLATFORM>, LIB_<NAME>_LIBS

# libjpeg-turbo (multi-platform: aarch64, imx6ull, rk3562)
LIB_JPEG_INC        := -I$(THIRD_LIB_ROOT)/libjpeg_turbo/include
LIB_JPEG_LIB_RK3562 := -L$(THIRD_LIB_ROOT)/libjpeg_turbo/lib_rk3562
LIB_JPEG_LIB_IMX6ULL:= -L$(THIRD_LIB_ROOT)/libjpeg_turbo/lib_imx6ull
LIB_JPEG_LIBS       := -ljpeg -lturbojpeg

# libyuv (multi-platform: aarch64, imx6ull - NO rk3562 version)
LIB_YUV_INC         := -I$(THIRD_LIB_ROOT)/libyuv/include
LIB_YUV_LIB_IMX6ULL := -L$(THIRD_LIB_ROOT)/libyuv/lib_imx6ull
LIB_YUV_LIBS        := -lyuv

# live555 (RTSP server, multi-platform: aarch64, imx6ull, rk3562)
# Note: Using --start-group/--end-group to handle circular dependencies
LIB_LIVE555_INC     := \
    -I$(THIRD_LIB_ROOT)/live555/include/liveMedia \
    -I$(THIRD_LIB_ROOT)/live555/include/groupsock \
    -I$(THIRD_LIB_ROOT)/live555/include/UsageEnvironment \
    -I$(THIRD_LIB_ROOT)/live555/include/BasicUsageEnvironment
LIB_LIVE555_LIB_RK3562 := -L$(THIRD_LIB_ROOT)/live555/lib_rk3562
LIB_LIVE555_LIB_IMX6ULL := -L$(THIRD_LIB_ROOT)/live555/lib_imx6ull
LIB_LIVE555_LIBS    := -lliveMedia -lgroupsock -lUsageEnvironment -lBasicUsageEnvironment 

# openh264 (multi-platform: aarch64, imx6ull - NO rk3562 version)
LIB_OPENH264_INC    := -I$(THIRD_LIB_ROOT)/openh264/include/wels
LIB_OPENH264_LIB_IMX6ULL := -L$(THIRD_LIB_ROOT)/openh264/lib_imx6ull
LIB_OPENH264_LIBS   := -lopenh264

# MNN (AI inference, multi-platform: aarch64, imx6ull, rk3562)
# Note: Used by both platforms (AI_ENGINE_MNN = 1 per board_option.h)
# Note: MNN is a shared library (.so), not static (.a)
LIB_MNN_INC         := -I$(THIRD_LIB_ROOT)/mnn/include/MNN
LIB_MNN_LIB_RK3562  := -L$(THIRD_LIB_ROOT)/mnn/lib_rk3562 -Wl,-rpath=$(THIRD_LIB_ROOT)/mnn/lib_rk3562
LIB_MNN_LIB_IMX6ULL := -L$(THIRD_LIB_ROOT)/mnn/lib_imx6ull -Wl,-rpath=$(THIRD_LIB_ROOT)/mnn/lib_imx6ull
LIB_MNN_LIBS        := -lMNN

# ------------------------------ RK3562-only Libraries ------------------------------
# These libraries only have RK3562 prebuilt versions

# RGA (Rockchip Graphics Acceleration)
LIB_RGA_INC         := -I$(THIRD_LIB_ROOT)/rkrga/include
LIB_RGA_LIB_RK3562  := -L$(THIRD_LIB_ROOT)/rkrga/lib_rk3562
LIB_RGA_LIBS        := -lrga

# MPP (Rockchip Media Process Platform)
LIB_MPP_INC         := -I$(THIRD_LIB_ROOT)/rkmpp/include
LIB_MPP_LIB_RK3562  := -L$(THIRD_LIB_ROOT)/rkmpp/lib_rk3562
LIB_MPP_LIBS        := -lrockchip_mpp

# RKNN (Rockchip NPU runtime) - DISABLED per board_option.h
# AI_ENGINE_RKNN = 0, using MNN instead
LIB_RKNN_INC        := 
LIB_RKNN_LIB_RK3562 := 
LIB_RKNN_LIBS       := 

# ------------------------------ Platform-specific Aggregated Flags ------------------------------

# RK3562 platform flags
THIRD_LIB_INC_RK3562 := \
    $(LIB_JPEG_INC) \
    $(LIB_YUV_INC) \
    $(LIB_LIVE555_INC) \
    $(LIB_OPENH264_INC) \
    $(LIB_MNN_INC) \
    $(LIB_RGA_INC) \
    $(LIB_MPP_INC) \
    $(LIB_RKNN_INC)

THIRD_LIB_LDFLAGS_RK3562 := \
    $(LIB_JPEG_LIB_RK3562) $(LIB_JPEG_LIBS) \
    $(LIB_LIVE555_LIB_RK3562) $(LIB_LIVE555_LIBS) \
    $(LIB_MNN_LIB_RK3562) $(LIB_MNN_LIBS) \
    $(LIB_RGA_LIB_RK3562) $(LIB_RGA_LIBS) \
    $(LIB_MPP_LIB_RK3562) $(LIB_MPP_LIBS)

# IMX6ULL platform flags (software libraries only)
THIRD_LIB_INC_IMX6ULL := \
    $(LIB_JPEG_INC) \
    $(LIB_YUV_INC) \
    $(LIB_LIVE555_INC) \
    $(LIB_OPENH264_INC) \
    $(LIB_MNN_INC)

THIRD_LIB_LDFLAGS_IMX6ULL := \
    $(LIB_JPEG_LIB_IMX6ULL) $(LIB_JPEG_LIBS) \
    $(LIB_YUV_LIB_IMX6ULL) $(LIB_YUV_LIBS) \
    $(LIB_LIVE555_LIB_IMX6ULL) $(LIB_LIVE555_LIBS) \
    $(LIB_OPENH264_LIB_IMX6ULL) $(LIB_OPENH264_LIBS) \
    $(LIB_MNN_LIB_IMX6ULL) $(LIB_MNN_LIBS)

# ------------------------------ Auto-select based on PLATFORM_* flag ------------------------------
ifeq ($(PLATFORM_RK3562),1)
    THIRD_LIB_INC      := $(THIRD_LIB_INC_RK3562)
    THIRD_LIB_LDFLAGS  := $(THIRD_LIB_LDFLAGS_RK3562)
else ifeq ($(PLATFORM_IMX6ULL),1)
    THIRD_LIB_INC      := $(THIRD_LIB_INC_IMX6ULL)
    THIRD_LIB_LDFLAGS  := $(THIRD_LIB_LDFLAGS_IMX6ULL)
else
    # Default: aarch64 (use RK3562 libs as fallback)
    THIRD_LIB_INC      := $(THIRD_LIB_INC_RK3562)
    THIRD_LIB_LDFLAGS  := $(THIRD_LIB_LDFLAGS_RK3562)
endif

# Export to sub-makefiles (critical for linking)
export THIRD_LIB_INC THIRD_LIB_LDFLAGS
