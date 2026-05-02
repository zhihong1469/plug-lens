# ==========================================================================
# src/Makefile：核心框架模块
# ==========================================================================

# 1. 源文件：所有核心框架的 .c
SRC_FILES := \
	app/main.c \
	bus/event_bus/src/event_bus.c \
	bus/data_bus/src/data_bus.c \
	fsm/global_fsm/src/global_fsm.c \
	fsm/module_fsm/src/module_fsm.c \
	link/frame_link/src/frame_link.c \
	service/capture_srv/src/capture_srv.c

# 2. 头文件路径：所有核心模块的 inc
INC_FLAGS := \
	-I$(TOPDIR)/src/hal/video/inc \
	-I$(TOPDIR)/src/bus/event_bus/inc \
	-I$(TOPDIR)/src/bus/data_bus/inc \
	-I$(TOPDIR)/src/fsm/global_fsm/inc \
	-I$(TOPDIR)/src/fsm/module_fsm/inc \
	-I$(TOPDIR)/src/link/frame_link/inc \
	-I$(TOPDIR)/src/service/capture_srv/inc \
	-I$(TOPDIR)/common/log/inc \
	-I$(TOPDIR)/common/queue/inc \
	-I$(TOPDIR)/common/plugin/inc \
	-I$(TOPDIR)/common/configs \
	-I$(TOPDIR)/plugins/hal_plugins/video_usb/inc \
	-I$(TOPDIR)/plugins/app_plugins/inc

# 3. 生成最终可执行文件
TARGET_NAME := vision_ai_app
TARGET_TYPE := EXE

# 4. 链接所有库：必须先libplug.a+再libcom.a+ pthread
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lplug \
	-lcom \
	-pthread