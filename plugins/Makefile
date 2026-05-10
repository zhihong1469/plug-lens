# ==========================================================================
# plugins/Makefile：插件模块（新增AI相关）
# ==========================================================================

# 1. 源文件（新增AI HAL、AI Link、人脸检测服务）
SRC_FILES := \
	hal_plugins/video_usb/src/video_hal.c \
	hal_plugins/video_usb/src/video_usb.c \
	hal_plugins/ai_model_mnn/src/ai_model_mnn.cpp \
	app_plugins/src/demo_app.c \
	service_plugins/capture_srv/src/capture_srv.c \
	service_plugins/face_detect_srv/src/face_detect_srv.c

# 2. 头文件路径（新增AI相关路径）
INC_FLAGS := \
	-I$(TOPDIR)/src/hal/video/inc \
	-I$(TOPDIR)/src/hal/ai_model/inc \
	-I$(TOPDIR)/src/link/frame_link/inc \
	-I$(TOPDIR)/src/link/ai_model_link/inc \
	-I$(TOPDIR)/src/bus/data_bus/inc \
	-I$(TOPDIR)/src/bus/event_bus/inc \
	-I$(TOPDIR)/src/fsm/global_fsm/inc \
	-I$(TOPDIR)/src/fsm/module_fsm/inc \
	-I$(TOPDIR)/src/service/capture_srv/inc \
	-I$(TOPDIR)/src/service/face_detect_srv/inc \
	-I$(TOPDIR)/plugins/hal_plugins/video_usb/inc \
	-I$(TOPDIR)/plugins/app_plugins/inc \
	-I$(TOPDIR)/common/log/inc \
	-I$(TOPDIR)/common/queue/inc \
	-I$(TOPDIR)/common/plugin/inc \
	-I$(TOPDIR)/common/configs \
	-I$(TOPDIR)/common/thread/inc \
	-I$(TOPDIR)/third_lib/face_detector/mnn/include \
	-I$(TOPDIR)/third_lib/opencv_lib/include/opencv4
# 3. 生成插件静态库
TARGET_NAME := libplug.a
TARGET_TYPE := LIB

# 4. 链接（注意：如果有C++库依赖，需要在这里添加，但我们先不链接，留到最后）
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lcom \
	-pthread