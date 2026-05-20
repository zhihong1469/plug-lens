# ==========================================================================
# 顶层 Makefile：修复版
# 核心改动：
# 1. 明确编译顺序：common → plugins → src
# 2. 修复路径处理逻辑
# 3. 支持 src/ 下多子模块编译
# ==========================================================================

# 0. 编译工具链：本地编译测试用，交叉编译时请取消注释上面的
# CROSS_COMPILE ?= arm-buildroot-linux-gnueabihf-
CROSS_COMPILE ?=
CC 			:= $(CROSS_COMPILE)gcc
CXX 		:= $(CROSS_COMPILE)g++
LD 			:= $(CROSS_COMPILE)ld
AR 			:= $(CROSS_COMPILE)ar
OBJCOPY 	:= $(CROSS_COMPILE)objcopy
OBJDUMP 	:= $(CROSS_COMPILE)objdump

# 自动获取交叉编译器 sysroot自动适配，无需手动填写
SYSROOT := $(shell $(CC) -print-sysroot)
# 标准系统头文件编译选项GCC/Clangd 通用
SYSROOT_CFLAGS := --sysroot=$(SYSROOT)
# 导出给子 Makefile.build 使用
export SYSROOT_CFLAGS

# 1. 定义目录路径 (绝对路径，防止混乱)
TOPDIR 		:= $(shell pwd)
SRCDIR 		:= $(TOPDIR)
BUILDDIR 	:= $(TOPDIR)/build
OUTPUTDIR 	:= $(TOPDIR)/output

# 定义 compile_commands.json 路径（放 output 目录，不污染源码）
COMPILE_COMMANDS := $(OUTPUTDIR)/compile_commands.json

# 导出变量，让子 Makefile.build 也能用到
export CC CXX LD AR OBJCOPY OBJDUMP 
export TOPDIR SRCDIR BUILDDIR OUTPUTDIR COMPILE_COMMANDS

# 2. 必须明确指定编译顺序（不自动扫描，避免顺序混乱）
# 顺序：common（公共库）→ plugins（插件）→ src（核心框架）
SUBDIRS := common plugins src

# 【核心】全局公共头文件路径（所有模块通用：common + src核心 + 第三方库）
# 所有子模块自动继承，无需重复定义！
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
	-I$(TOPDIR)/common/configs \
	-I$(TOPDIR)/src/base/camera/inc \
	-I$(TOPDIR)/src/base/ai_model/inc \
	-I$(TOPDIR)/src/base/service/inc \
	-I$(TOPDIR)/src/bus/event_bus/inc \
	-I$(TOPDIR)/src/bus/data_bus/inc \
	-I$(TOPDIR)/src/app/inc \
	-I$(TOPDIR)/third_lib/face_detector/mnn/include \
	-I$(TOPDIR)/third_lib/opencv_lib/include/opencv4 \
	-I$(TOPDIR)/third_lib/tlsf-master

# 导出全局路径（所有子模块自动可用）
export GLOBAL_INC

# 3. 目标列表
TARGETS := $(patsubst %, build-%, $(SUBDIRS))

# 默认目标
all: $(OUTPUTDIR) $(BUILDDIR) init_compile_commands $(TARGETS) finalize_compile_commands
	@echo "=========================================="
	@echo "Build complete! Output: $(OUTPUTDIR)"
	@echo "=========================================="

# 创建输出目录
$(OUTPUTDIR):
	mkdir -p $@

# 创建构建根目录
$(BUILDDIR):
	mkdir -p $@

# 初始化 compile_commands.json（清空旧文件，写入 [ 开头）
init_compile_commands: $(OUTPUTDIR)
	@echo "[" > $(COMPILE_COMMANDS)

# 收尾 compile_commands.json（去掉最后一个逗号，写入 ] 结尾）
finalize_compile_commands:
	@sed -i '$$s/,$$//' $(COMPILE_COMMANDS)
	@echo "]" >> $(COMPILE_COMMANDS)
	@echo "Generated: $(COMPILE_COMMANDS)"

# 4. 核心编译规则
$(TARGETS): build-%:
	@mkdir -p $(BUILDDIR)/$*
	@echo "=========================================="
	@echo "Building : $*"
	@echo "=========================================="
	@make -C $(BUILDDIR)/$* -f $(TOPDIR)/Makefile.build CUR_DIR=$*

# 清理规则
clean:
	rm -rf $(BUILDDIR) $(OUTPUTDIR)

# 声明伪目标
.PHONY: all clean $(TARGETS) init_compile_commands finalize_compile_commands