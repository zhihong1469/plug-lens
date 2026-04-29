# ==========================================================================
# app/Makefile：应用层
# ==========================================================================

# 1. 源文件（相对当前makefile目录，后续加直接追加）
SRC_FILES := \
	app/main.c \

# 2. 头文件路径（包含BSP的头文件）
INC_FLAGS := \
	-I$(TOPDIR)/common/log/inc \
	-I$(TOPDIR)/common/queue/inc \
	-I$(TOPDIR)/plugins/hal_plugins/video_usb/inc

# 3. 生成可执行文件
TARGET_NAME := vision_ai_app
TARGET_TYPE := EXE

# 4. 链接 libbsp.a
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lcom \
	-lplug \
	-pthread