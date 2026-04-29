# ==========================================================================
# plugins/Makefile：插件层
# ==========================================================================

# 1. 源文件（相对于 plugins/ 目录）
SRC_FILES := \
	hal_plugins/video_usb/src/v4l2_video.c \
	hal_plugins/video_usb/src/v4l2_hal.c

# 2. 头文件路径
INC_FLAGS := \
	-I$(TOPDIR)/src/hal/video/inc \
	-I$(TOPDIR)/plugins/hal_plugins/video_usb/inc \
	-I$(TOPDIR)/common/log/inc \
	-I$(TOPDIR)/common/queue/inc

# 3. 生成插件静态库
TARGET_NAME := libplug.a
TARGET_TYPE := LIB

# 4. 链接 
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lcom \
	-pthread