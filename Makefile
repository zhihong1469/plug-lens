# ==========================================================================
# Top-Level Makefile (Unified Configuration Version)
# @brief  Main build script with intelligent configuration
# @key-features:
# 1. Auto-detect toolchain based on platform/engine selection
# 2. Unified configuration via common/configs/build_config.mk
# 3. Support command-line overrides for quick testing
# @author LuoZhihong
# @license MIT License
# ==========================================================================

# ===================== Project Directory Paths =====================
TOPDIR 		:= $(shell pwd)
SRCDIR 		:= $(TOPDIR)
BUILDDIR 	:= $(TOPDIR)/build
OUTPUTDIR 	:= $(TOPDIR)/output
COMPILE_COMMANDS := $(OUTPUTDIR)/compile_commands.json

# ===================== Load Configuration =====================
# Priority: command-line > common/configs/build_config.mk > defaults
-include $(TOPDIR)/common/configs/build_config.mk

# ===================== Platform & Engine Mapping =====================
# Use TARGET_PLATFORM to avoid conflict with Make's built-in PLATFORM variable
TARGET_PLATFORM ?= rk3562
ENGINE          ?= software

ifeq ($(TARGET_PLATFORM),rk3562)
    PLATFORM_RK3562 := 1
    PLATFORM_IMX6ULL := 0
else ifeq ($(TARGET_PLATFORM),imx6ull)
    PLATFORM_RK3562 := 0
    PLATFORM_IMX6ULL := 1
else
    $(error Invalid TARGET_PLATFORM: $(TARGET_PLATFORM). Must be 'rk3562' or 'imx6ull')
endif

ifeq ($(TARGET_PLATFORM),rk3562)
    ifeq ($(ENGINE),hardware)
        IMG_PROC_RGA := 1
        VIDEO_ENCODER_MPP := 1
        AI_ENGINE_RKNN := 1
        AI_ENGINE_MNN := 0
    else ifeq ($(ENGINE),software)
        IMG_PROC_RGA := 0
        VIDEO_ENCODER_MPP := 0
        AI_ENGINE_RKNN := 0
        AI_ENGINE_MNN := 1
    else
        $(error Invalid ENGINE: $(ENGINE). Must be 'hardware' or 'software')
    endif
else
    IMG_PROC_RGA := 0
    VIDEO_ENCODER_MPP := 0
    AI_ENGINE_RKNN := 0
    AI_ENGINE_MNN := 1
endif

# ===================== Toolchain Auto-selection =====================
ifndef CROSS_COMPILE
    ifeq ($(TARGET_PLATFORM),rk3562)
        ifeq ($(ENGINE),hardware)
            TOOLCHAIN_PATH := /usr/local/arm/gcc-linaro-10.3.1-2021.07-x86_64_aarch64-linux-gnu/bin
            CROSS_COMPILE := $(TOOLCHAIN_PATH)/aarch64-linux-gnu-
        else
            TOOLCHAIN_PATH := /usr/local/arm/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin
            CROSS_COMPILE := $(TOOLCHAIN_PATH)/aarch64-linux-gnu-
        endif
    else ifeq ($(TARGET_PLATFORM),imx6ull)
        TOOLCHAIN_PATH := /usr/local/arm/gcc-linaro-6.2.1-2016.11-x86_64_arm-linux-gnueabihf/bin
        CROSS_COMPILE := $(TOOLCHAIN_PATH)/arm-linux-gnueabihf-
    endif
    $(info Auto-selected toolchain: $(CROSS_COMPILE))
endif

CC 			:= $(CROSS_COMPILE)gcc
CXX 		:= $(CROSS_COMPILE)g++
LD 			:= $(CROSS_COMPILE)ld
AR 			:= $(CROSS_COMPILE)ar
OBJCOPY 	:= $(CROSS_COMPILE)objcopy
OBJDUMP 	:= $(CROSS_COMPILE)objdump

SYSROOT := $(shell $(CC) -print-sysroot)
SYSROOT_CFLAGS := --sysroot=$(SYSROOT)

# ===================== Third-party Library Configuration =====================
include $(TOPDIR)/third_lib/third_lib.mk

# ===================== Build Order Configuration =====================
SUBDIRS := common plugins src

# ===================== Global Include Paths =====================
GLOBAL_INC := \
	-I$(TOPDIR)/common/log/inc \
	-I$(TOPDIR)/common/queue/inc \
	-I$(TOPDIR)/common/thread/inc \
	-I$(TOPDIR)/common/utils/inc \
	-I$(TOPDIR)/common/plugin/inc \
	-I$(TOPDIR)/common/pool/inc \
	-I$(TOPDIR)/common/img_proc/inc \
	-I$(TOPDIR)/common/initcall/inc \
	-I$(TOPDIR)/common/mem_adapter/inc \
	-I$(TOPDIR)/common/daemon/inc \
	-I$(TOPDIR)/common/sd_mount/inc \
	-I$(TOPDIR)/common/network/inc \
	-I$(TOPDIR)/common/sys_time/inc \
	-I$(TOPDIR)/common/configs \
	-I$(TOPDIR)/src/base/camera/inc \
	-I$(TOPDIR)/src/base/ai_model/inc \
	-I$(TOPDIR)/src/base/img_proc/inc \
	-I$(TOPDIR)/src/base/led/inc \
	-I$(TOPDIR)/src/bus/event_bus/inc \
	-I$(TOPDIR)/src/bus/data_bus/inc \
	-I$(TOPDIR)/src/app/inc \
	-I$(TOPDIR)/third_lib/tlsf-master \
	-I$(TOPDIR)/plugins/base_plugins/camera_usb/inc \
	-I$(TOPDIR)/plugins/base_plugins/ai_model_mnn/inc \
	-I$(TOPDIR)/plugins/base_plugins/ai_model_rknn/inc \
	-I$(TOPDIR)/plugins/base_plugins/img_joint/inc \
	-I$(TOPDIR)/plugins/base_plugins/img_rga/inc \

GLOBAL_INC += $(THIRD_LIB_INC)

# ===================== Export Configuration =====================
export PLATFORM PLATFORM_RK3562 PLATFORM_IMX6ULL
export ENGINE IMG_PROC_RGA VIDEO_ENCODER_MPP AI_ENGINE_RKNN AI_ENGINE_MNN
export CC CXX LD AR OBJCOPY OBJDUMP 
export TOPDIR SRCDIR BUILDDIR OUTPUTDIR COMPILE_COMMANDS SYSROOT_CFLAGS
export GLOBAL_INC THIRD_LIB_INC THIRD_LIB_LDFLAGS

# ===================== Build Targets =====================
TARGETS := $(patsubst %, build-%, $(SUBDIRS))

all: $(OUTPUTDIR) $(BUILDDIR) init_compile_commands $(TARGETS) finalize_compile_commands
	@echo "=========================================="
	@echo "Build complete! Output: $(OUTPUTDIR)"
	@echo "=========================================="

$(OUTPUTDIR):
	mkdir -p $@

$(BUILDDIR):
	mkdir -p $@

init_compile_commands: $(OUTPUTDIR)
	@echo "[" > $(COMPILE_COMMANDS)

finalize_compile_commands:
	@sed -i '$$s/,$$//' $(COMPILE_COMMANDS)
	@echo "]" >> $(COMPILE_COMMANDS)
	@echo "Generated: $(COMPILE_COMMANDS)"

$(TARGETS): build-%:
	@mkdir -p $(BUILDDIR)/$*
	@echo "=========================================="
	@echo "Building : $*"
	@echo "=========================================="
	@make -C $(BUILDDIR)/$* -f $(TOPDIR)/Makefile.build CUR_DIR=$*

clean:
	rm -rf $(BUILDDIR) $(OUTPUTDIR)

.PHONY: all clean $(TARGETS) init_compile_commands finalize_compile_commands