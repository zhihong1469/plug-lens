# ==========================================================================
# plugins/Makefile：插件模块
# ==========================================================================

# 1. 源文件（相对于 plugins/ 目录）
SRC_FILES := \
	hal_plugins/video_usb/src/video_hal.c \
	hal_plugins/video_usb/src/video_usb.c \
	app_plugins/src/demo_app.c \
	service_plugins/capture_srv/src/capture_srv.c

# 2. 头文件路径
INC_FLAGS := \
	-I$(TOPDIR)/src/hal/video/inc \
	-I$(TOPDIR)/src/link/frame_link/inc \
	-I$(TOPDIR)/src/bus/data_bus/inc \
	-I$(TOPDIR)/src/bus/event_bus/inc \
	-I$(TOPDIR)/src/fsm/global_fsm/inc \
	-I$(TOPDIR)/src/fsm/module_fsm/inc \
	-I$(TOPDIR)/src/service/capture_srv/inc \
	-I$(TOPDIR)/plugins/hal_plugins/video_usb/inc \
	-I$(TOPDIR)/plugins/app_plugins/inc \
	-I$(TOPDIR)/common/log/inc \
	-I$(TOPDIR)/common/queue/inc \
	-I$(TOPDIR)/common/plugin/inc \
	-I$(TOPDIR)/common/configs

# 3. 生成插件静态库
TARGET_NAME := libplug.a
TARGET_TYPE := LIB

# 4. 链接 
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lcom \
	-pthread