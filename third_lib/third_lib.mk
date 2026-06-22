# ==========================================================================
# Third-party Library Configuration
# @brief  Centralized configuration for all third-party libraries
# @usage  Include this file in Makefiles that need third-party library access
# @note   Supports three toolchains with existing directory structure:
#         - arm64-linux-75:   RK3562 software mode → uses lib_aarch64
#         - arm64-linux-103:  RK3562 hardware mode → uses lib_rk3562  
#         - arm32-linux-hf6ull: i.MX6ULL → uses lib_imx6ull
# ==========================================================================

THIRD_LIB_ROOT := $(TOPDIR)/third_lib

# ------------------------------ Common Libraries ------------------------------
# libjpeg-turbo
LIB_JPEG_INC        := -I$(THIRD_LIB_ROOT)/libjpeg_turbo/include
LIB_JPEG_LIB_RK3562_SW  := -L$(THIRD_LIB_ROOT)/libjpeg_turbo/lib_aarch64  # arm64-linux-75
LIB_JPEG_LIB_RK3562_HW  := -L$(THIRD_LIB_ROOT)/libjpeg_turbo/lib_rk3562   # arm64-linux-103
LIB_JPEG_LIB_IMX6ULL    := -L$(THIRD_LIB_ROOT)/libjpeg_turbo/lib_imx6ull   # arm32-linux-hf6ull
LIB_JPEG_LIBS       := -ljpeg -lturbojpeg

# libyuv
LIB_YUV_INC         := -I$(THIRD_LIB_ROOT)/libyuv/include
LIB_YUV_LIB_RK3562_SW   := -L$(THIRD_LIB_ROOT)/libyuv/lib_aarch64
LIB_YUV_LIB_RK3562_HW   := -L$(THIRD_LIB_ROOT)/libyuv/lib_aarch64        # Reuse aarch64
LIB_YUV_LIB_IMX6ULL     := -L$(THIRD_LIB_ROOT)/libyuv/lib_imx6ull
LIB_YUV_LIBS        := -lyuv

# live555
LIB_LIVE555_INC     := \
    -I$(THIRD_LIB_ROOT)/live555/include/liveMedia \
    -I$(THIRD_LIB_ROOT)/live555/include/groupsock \
    -I$(THIRD_LIB_ROOT)/live555/include/UsageEnvironment \
    -I$(THIRD_LIB_ROOT)/live555/include/BasicUsageEnvironment
LIB_LIVE555_LIB_RK3562_SW := -L$(THIRD_LIB_ROOT)/live555/lib_aarch64
LIB_LIVE555_LIB_RK3562_HW := -L$(THIRD_LIB_ROOT)/live555/lib_rk3562
LIB_LIVE555_LIB_IMX6ULL := -L$(THIRD_LIB_ROOT)/live555/lib_imx6ull
LIB_LIVE555_LIBS    := -lliveMedia -lgroupsock -lUsageEnvironment -lBasicUsageEnvironment 

# openh264
LIB_OPENH264_INC    := -I$(THIRD_LIB_ROOT)/openh264/include/wels
LIB_OPENH264_LIB_RK3562_SW := -L$(THIRD_LIB_ROOT)/openh264/lib_aarch64
LIB_OPENH264_LIB_RK3562_HW := -L$(THIRD_LIB_ROOT)/openh264/lib_aarch64   # Reuse aarch64
LIB_OPENH264_LIB_IMX6ULL := -L$(THIRD_LIB_ROOT)/openh264/lib_imx6ull
LIB_OPENH264_LIBS   := -lopenh264

# MNN
LIB_MNN_INC         := -I$(THIRD_LIB_ROOT)/mnn/include/MNN
LIB_MNN_LIB_RK3562_SW  := -L$(THIRD_LIB_ROOT)/mnn/lib_aarch64 -Wl,-rpath=$(THIRD_LIB_ROOT)/mnn/lib_aarch64
LIB_MNN_LIB_RK3562_HW  := -L$(THIRD_LIB_ROOT)/mnn/lib_rk3562 -Wl,-rpath=$(THIRD_LIB_ROOT)/mnn/lib_rk3562
LIB_MNN_LIB_IMX6ULL    := -L$(THIRD_LIB_ROOT)/mnn/lib_imx6ull -Wl,-rpath=$(THIRD_LIB_ROOT)/mnn/lib_imx6ull
LIB_MNN_LIBS        := -lMNN

# ------------------------------ RK3562 Hardware-specific Libraries ------------------------------
# RGA
ifeq ($(IMG_PROC_RGA),1)
LIB_RGA_INC         := -I$(THIRD_LIB_ROOT)/rkrga/include
LIB_RGA_LIB_RK3562  := -L$(THIRD_LIB_ROOT)/rkrga/lib_rk3562
LIB_RGA_LIBS        := -lrga
else
LIB_RGA_INC         :=
LIB_RGA_LIB_RK3562  :=
LIB_RGA_LIBS        :=
endif

# MPP
ifeq ($(VIDEO_ENCODER_MPP),1)
LIB_MPP_INC         := -I$(THIRD_LIB_ROOT)/rkmpp/include
LIB_MPP_LIB_RK3562  := -L$(THIRD_LIB_ROOT)/rkmpp/lib_rk3562
LIB_MPP_LIBS        := -lrockchip_mpp
else
LIB_MPP_INC         :=
LIB_MPP_LIB_RK3562  :=
LIB_MPP_LIBS        :=
endif

# RKNN
ifeq ($(AI_ENGINE_RKNN),1)
LIB_RKNN_INC        := -I$(THIRD_LIB_ROOT)/rknn/include
LIB_RKNN_LIB_RK3562 := -L$(THIRD_LIB_ROOT)/rknn/lib_rk3562 -Wl,-rpath=$(THIRD_LIB_ROOT)/rknn/lib_rk3562
LIB_RKNN_LIBS       := -lrknnrt
else
LIB_RKNN_INC        :=
LIB_RKNN_LIB_RK3562 :=
LIB_RKNN_LIBS       :=
endif

# ------------------------------ Platform-specific Aggregated Flags ------------------------------

# RK3562 Software Mode (arm64-linux-75)
THIRD_LIB_INC_RK3562_SW := \
    $(LIB_JPEG_INC) \
    $(LIB_YUV_INC) \
    $(LIB_LIVE555_INC) \
    $(LIB_OPENH264_INC) \
    $(LIB_MNN_INC)

THIRD_LIB_LDFLAGS_RK3562_SW := \
    $(LIB_JPEG_LIB_RK3562_SW) $(LIB_JPEG_LIBS) \
    $(LIB_YUV_LIB_RK3562_SW) $(LIB_YUV_LIBS) \
    $(LIB_LIVE555_LIB_RK3562_SW) $(LIB_LIVE555_LIBS) \
    $(LIB_OPENH264_LIB_RK3562_SW) $(LIB_OPENH264_LIBS) \
    $(LIB_MNN_LIB_RK3562_SW) $(LIB_MNN_LIBS)

# RK3562 Hardware Mode (arm64-linux-103)
THIRD_LIB_INC_RK3562_HW := \
    $(LIB_JPEG_INC) \
    $(LIB_YUV_INC) \
    $(LIB_LIVE555_INC) \
    $(LIB_OPENH264_INC) \
    $(LIB_MNN_INC) \
    $(LIB_RGA_INC) \
    $(LIB_MPP_INC) \
    $(LIB_RKNN_INC)

THIRD_LIB_LDFLAGS_RK3562_HW := \
    $(LIB_JPEG_LIB_RK3562_HW) $(LIB_JPEG_LIBS) \
    $(LIB_YUV_LIB_RK3562_HW) $(LIB_YUV_LIBS) \
    $(LIB_LIVE555_LIB_RK3562_HW) $(LIB_LIVE555_LIBS) \
    $(LIB_OPENH264_LIB_RK3562_HW) $(LIB_OPENH264_LIBS) \
    $(LIB_MNN_LIB_RK3562_HW) $(LIB_MNN_LIBS) \
    $(LIB_RGA_LIB_RK3562) $(LIB_RGA_LIBS) \
    $(LIB_MPP_LIB_RK3562) $(LIB_MPP_LIBS) \
    $(LIB_RKNN_LIB_RK3562) $(LIB_RKNN_LIBS)

# IMX6ULL (arm32-linux-hf6ull)
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

# ------------------------------ Auto-select based on PLATFORM_RK3562 and ENGINE ------------------------------
ifeq ($(PLATFORM_RK3562),1)
    ifeq ($(ENGINE),hardware)
        THIRD_LIB_INC      := $(THIRD_LIB_INC_RK3562_HW)
        THIRD_LIB_LDFLAGS  := $(THIRD_LIB_LDFLAGS_RK3562_HW)
    else
        THIRD_LIB_INC      := $(THIRD_LIB_INC_RK3562_SW)
        THIRD_LIB_LDFLAGS  := $(THIRD_LIB_LDFLAGS_RK3562_SW)
    endif
else ifeq ($(PLATFORM_IMX6ULL),1)
    THIRD_LIB_INC      := $(THIRD_LIB_INC_IMX6ULL)
    THIRD_LIB_LDFLAGS  := $(THIRD_LIB_LDFLAGS_IMX6ULL)
else
    THIRD_LIB_INC      := $(THIRD_LIB_INC_RK3562_SW)
    THIRD_LIB_LDFLAGS  := $(THIRD_LIB_LDFLAGS_RK3562_SW)
endif

export THIRD_LIB_INC THIRD_LIB_LDFLAGS