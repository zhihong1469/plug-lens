# ==========================================================================
# 顶层 Makefile：自动扫描子目录 + 控制编译顺序 + 生成 compile_commands.json
# ==========================================================================

# 0. 定义交叉编译工具链
CROSS_COMPILE ?= arm-buildroot-linux-gnueabihf-
CC 			:= $(CROSS_COMPILE)gcc
LD 			:= $(CROSS_COMPILE)ld
AR 			:= $(CROSS_COMPILE)ar
OBJCOPY 	:= $(CROSS_COMPILE)objcopy
OBJDUMP 	:= $(CROSS_COMPILE)objdump

# 自动获取交叉编译器 sysroot自动适配，无需手动填写
SYSROOT := $(shell $(CC) -print-sysroot)
# 标准系统头文件编译选项GCC/Clangd 通用
SYSROOT_CFLAGS := -isystem $(SYSROOT)/usr/include --sysroot=$(SYSROOT)
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
export CC LD AR OBJCOPY OBJDUMP
export TOPDIR SRCDIR BUILDDIR OUTPUTDIR COMPILE_COMMANDS

# 2. 必须明确指定编译顺序（不自动扫描，避免顺序混乱）
# 顺序：common（公共库）→ plugins（插件）→ src（核心框架）
SUBDIRS := common plugins src

# 3. 定义目标列表：格式为 build-xxx
TARGETS := $(patsubst %, build-%, $(SUBDIRS))

# 默认目标
all: $(OUTPUTDIR) $(BUILDDIR) init_compile_commands $(TARGETS) finalize_compile_commands
	@echo "=========================================="
	@echo "Build complete! Check output directory."
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
	@# 这是一个小技巧：用 sed 去掉文件倒数第二行的逗号（JSON 格式要求）
	@sed -i '$$s/,$$//' $(COMPILE_COMMANDS)
	@echo "]" >> $(COMPILE_COMMANDS)
	@echo "Generated: $(COMPILE_COMMANDS)"

# 4. 核心规则：针对每一个子目录，调用 Makefile.build
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