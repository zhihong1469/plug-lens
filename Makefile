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

# 2. 自动扫描子目录 + 强制 BSP 优先编译（修复版）
# 逻辑：
# 1. find 找到所有子 Makefile（绝对路径）
# 2. 用 patsubst 把 $(TOPDIR)/ 前缀去掉，变成相对路径（如 app/Makefile）
# 3. 提取目录名（如 app）
# 4. BSP 放前面，其他放后面
SUB_MAKEFILES := $(shell find $(SRCDIR) -name "Makefile" \
					-not -path "$(SRCDIR)/Makefile" \
					-not -path "$(BUILDDIR)/%")

# 【关键修复】把绝对路径转成相对于 TOPDIR 的短路径
# 例如：/home/luo/.../app/Makefile -> app/Makefile
SUB_MAKEFILES_REL := $(patsubst $(TOPDIR)/%,%,$(SUB_MAKEFILES))

# 提取目录名（如 app/Makefile -> app）
SUBDIRS_RAW := $(patsubst %/,%,$(dir $(SUB_MAKEFILES_REL)))

# 排序：BSP 优先，其他在后
SUBDIRS := $(filter BSP,$(SUBDIRS_RAW)) $(filter-out BSP,$(SUBDIRS_RAW))

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
init_compile_commands:
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
	@echo "Building Module: $*"
	@echo "=========================================="
	@make -C $(BUILDDIR)/$* -f $(TOPDIR)/Makefile.build CUR_DIR=$*

# 清理规则
clean:
	rm -rf $(BUILDDIR) $(OUTPUTDIR)

# 声明伪目标
.PHONY: all clean $(TARGETS) init_compile_commands finalize_compile_commands