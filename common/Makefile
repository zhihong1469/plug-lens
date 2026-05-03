# ==========================================================================
# common/Makefile：公共组件库
# ==========================================================================

# 1. 源文件（相对于 common/ 目录，直接写子路径）
SRC_FILES := \
	log/src/log.c \
	queue/src/queue.c \
	plugin/src/plugin_loader.c

# 2. 头文件路径
INC_FLAGS := \
	-I$(TOPDIR)/common/log/inc \
	-I$(TOPDIR)/common/queue/inc \
	-I$(TOPDIR)/common/thread/inc \
	-I$(TOPDIR)/common/utils/inc \
	-I$(TOPDIR)/common/plugin/inc \
	-I$(TOPDIR)/common/configs

# 3. 生成静态库
TARGET_NAME := libcom.a
TARGET_TYPE := LIB

LDFLAGS :=