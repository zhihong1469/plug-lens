# ==========================================================================
# Top-Level Makefile (Fixed Version)
# @brief  Main build script for embedded Linux project
# @key-changes:
# 1. Fixed build order: common → plugins → src
# 2. Fixed path handling logic
# 3. Support multi-submodule compilation under src/
# @author LuoZhihong
# @license MIT License
# ==========================================================================

# ===================== Platform Configuration =====================
# @brief  Platform selection (must match board_option.h)
# @usage  Set PLATFORM_RK3562=1 for RK3562, or PLATFORM_IMX6ULL=1 for i.MX6ULL
# @note   Default: RK3562 platform
PLATFORM_RK3562 ?= 1
PLATFORM_IMX6ULL ?= 0

# @brief  Export platform configuration to sub-makefiles
export PLATFORM_RK3562 PLATFORM_IMX6ULL

# ===================== Cross-Compiler Toolchain =====================
# @brief  Toolchain configuration (native for test, cross-compile for embedded)
# @usage  Before building, select toolchain:
#         - i.MX6ULL: use_toolchain arm32-linux-hf6ull
#         - RK3562:   use_toolchain arm64-linux-75
# CROSS_COMPILE ?= arm-buildroot-linux-gnueabihf-
CROSS_COMPILE ?=
CC 			:= $(CROSS_COMPILE)gcc
CXX 		:= $(CROSS_COMPILE)g++
LD 			:= $(CROSS_COMPILE)ld
AR 			:= $(CROSS_COMPILE)ar
OBJCOPY 	:= $(CROSS_COMPILE)objcopy
OBJDUMP 	:= $(CROSS_COMPILE)objdump

# @brief  Auto-detect compiler sysroot (auto-adapt for cross-compile)
SYSROOT := $(shell $(CC) -print-sysroot)
# @brief  Standard sysroot CFLAGS (compatible with GCC/Clangd)
SYSROOT_CFLAGS := --sysroot=$(SYSROOT)
# @brief  Export to sub-Makefile.build
export SYSROOT_CFLAGS

# ===================== Project Directory Paths =====================
# @brief  Top directory (absolute path to avoid path confusion)
TOPDIR 		:= $(shell pwd)
SRCDIR 		:= $(TOPDIR)
BUILDDIR 	:= $(TOPDIR)/build
OUTPUTDIR 	:= $(TOPDIR)/output

# @brief  compile_commands.json path (for IDE/Clangd code completion)
COMPILE_COMMANDS := $(OUTPUTDIR)/compile_commands.json

# @brief  Export variables to sub-makefiles
export CC CXX LD AR OBJCOPY OBJDUMP 
export TOPDIR SRCDIR BUILDDIR OUTPUTDIR COMPILE_COMMANDS

# ===================== Third-party Library Configuration =====================
# @brief  Centralized third-party library paths (see third_lib/third_lib.mk)
include $(TOPDIR)/third_lib/third_lib.mk

# ===================== Build Order Configuration =====================
# @brief  Fixed build order (critical for dependency resolution)
# @order  1. common   (base library)
#         2. plugins  (depend on common)
#         3. src      (main application, depend on all)
SUBDIRS := common plugins src

# ===================== Global Include Paths =====================
# @brief  Global public includes (all modules inherit automatically)
# @note   No repeated definition required in submodules
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

# @brief  RK3562 platform specific includes (unified via third_lib.mk)
GLOBAL_INC += \
	$(THIRD_LIB_INC)

# @brief  Software libraries - only needed for i.MX6ULL (RK3562 uses hardware acceleration)
# Note: THIRD_LIB_INC already handles platform-specific paths automatically

# @brief  Export global includes to all submodules
export GLOBAL_INC

# ===================== Build Targets =====================
# @brief  Generate build targets for each subdirectory
TARGETS := $(patsubst %, build-%, $(SUBDIRS))

# ===================== Default Build Target =====================
# @brief  Main entry: create dirs → generate compile_commands → build all
all: $(OUTPUTDIR) $(BUILDDIR) init_compile_commands $(TARGETS) finalize_compile_commands
	@echo "=========================================="
	@echo "Build complete! Output: $(OUTPUTDIR)"
	@echo "=========================================="

# @brief  Create output directory
$(OUTPUTDIR):
	mkdir -p $@

# @brief  Create build root directory
$(BUILDDIR):
	mkdir -p $@

# ===================== compile_commands.json Generation =====================
# @brief  Initialize compile_commands.json (clear old file, write [)
init_compile_commands: $(OUTPUTDIR)
	@echo "[" > $(COMPILE_COMMANDS)

# @brief  Finalize compile_commands.json (remove last comma, write ])
finalize_compile_commands:
	@sed -i '$$s/,$$//' $(COMPILE_COMMANDS)
	@echo "]" >> $(COMPILE_COMMANDS)
	@echo "Generated: $(COMPILE_COMMANDS)"

# ===================== Core Build Rules =====================
# @brief  Build subdirectory target (call Makefile.build)
$(TARGETS): build-%:
	@mkdir -p $(BUILDDIR)/$*
	@echo "=========================================="
	@echo "Building : $*"
	@echo "=========================================="
	@make -C $(BUILDDIR)/$* -f $(TOPDIR)/Makefile.build CUR_DIR=$*

# ===================== Clean Rule =====================
# @brief  Clean all build and output files
clean:
	rm -rf $(BUILDDIR) $(OUTPUTDIR)

# ===================== Phony Targets =====================
# @brief  Declare phony targets (not files)
.PHONY: all clean $(TARGETS) init_compile_commands finalize_compile_commands