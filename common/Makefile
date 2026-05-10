# ==========================================================================
# common/Makefile：公共组件库
# ==========================================================================

# 1. 源文件（相对于 common/ 目录，直接写子路径）
SRC_FILES := \
	log/src/log.c \
	queue/src/queue.c \
	plugin/src/plugin_loader.c \
	pool/src/pool.c \
	utils/src/utils.c \
	thread/src/thread.c \
	img_proc/src/img_proc_c.c \

# 2. 头文件路径
INC_FLAGS := \
	-I$(TOPDIR)/common/log/inc \
	-I$(TOPDIR)/common/queue/inc \
	-I$(TOPDIR)/common/thread/inc \
	-I$(TOPDIR)/common/utils/inc \
	-I$(TOPDIR)/common/plugin/inc \
	-I$(TOPDIR)/common/configs \
	-I$(TOPDIR)/common/img_proc/inc \
	-I$(TOPDIR)/common/pool/inc \
	-I$(TOPDIR)/common/utils/inc \
	-I$(TOPDIR)/common/thread/inc

# 3. 生成静态库
TARGET_NAME := libcom.a
TARGET_TYPE := LIB

LDFLAGS :=