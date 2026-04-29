# ==========================================================================
# app/Makefile：应用层
# ==========================================================================

# 1. 源文件（相对app目录：main/main.c，后续加直接追加）
SRC_FILES := \
	/hal_plugins/video_usb/v4l2_app.c \
	/hal_plugins/video_usb/v4l2_video.c

# 2. 头文件路径
INC_FLAGS := \
	-I$(TOPDIR)/plugins/hal_plugins/inc
	-I$(TOPDIR)/plugins/hal_plugins/video_usb/inc
# 3. 生成可执行文件
TARGET_NAME := libpulg.a
TARGET_TYPE := LIB

# 4. 链接 
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lcom \
	-pthread