# ==========================================================================
# app/Makefile：主应用模块配置
# ==========================================================================

# 1. 源文件（相对于 app/ 目录）
SRC_FILES := src/main.c

# 2. 头文件路径（必须包含 lib/inc，否则找不到 log.h）
INC_FLAGS := \
	-I$(SRC_DIR)/inc \
	-I$(TOPDIR)/lib/inc

# 3. 定义目标：可执行文件 vision_ai_app
TARGET_NAME := vision_ai_app
TARGET_TYPE := EXE

# 4. 链接选项：链接 liblog.a（-L 找库路径，-l 链接库）
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-llog