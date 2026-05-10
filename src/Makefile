# ==========================================================================
# src/Makefile：核心框架模块（新增AI Link路径）
# ==========================================================================

# 1. 源文件（新增 ai_model_link.cpp）
SRC_FILES := \
	app/app_camera.c \
	device/camera/src/camera_base.c

# 	bus/event_bus/src/event_bus.c \
# 	bus/data_bus/src/data_bus.c \
# 	fsm/global_fsm/src/global_fsm.c \
# 	fsm/module_fsm/src/module_fsm.c \
# 	link/frame_link/src/frame_link.c \
# 	link/ai_model_link/src/ai_model_link.cpp 
	

# 2. 头文件路径（新增AI相关）
INC_FLAGS := \
	-I$(TOPDIR)/src/device/camera/inc \
	-I$(TOPDIR)/plugins/device_plugins/camera_usb/inc \
	-I$(TOPDIR)/common/log/inc
# 	-I$(TOPDIR)/common/queue/inc \
# 	-I$(TOPDIR)/common/pool/inc \
# 	-I$(TOPDIR)/common/utils/inc \
# 	-I$(TOPDIR)/common/thread/inc \
# 	-I$(TOPDIR)/common/plugin/inc \
# 	-I$(TOPDIR)/common/configs \

# 	-I$(TOPDIR)/plugins/app_plugins/inc \
# 	-I$(TOPDIR)/third_lib/face_detector/mnn/include \
# 	-I$(TOPDIR)/third_lib/opencv_lib/include/opencv4
# 	-I$(TOPDIR)/src/device/ai_model/inc \
# 	-I$(TOPDIR)/src/bus/event_bus/inc \
# 	-I$(TOPDIR)/src/bus/data_bus/inc \
# 	-I$(TOPDIR)/src/fsm/global_fsm/inc \
# 	-I$(TOPDIR)/src/fsm/module_fsm/inc \
# 	-I$(TOPDIR)/src/link/frame_link/inc \
# 	-I$(TOPDIR)/src/link/ai_model_link/inc \
# 	-I$(TOPDIR)/src/service/capture_srv/inc \
# 	-I$(TOPDIR)/src/service/face_detect_srv/inc 
# 3. 生成最终可执行文件
TARGET_NAME := vision_ai_app
TARGET_TYPE := EXE

# 4. 链接所有库：必须先用先链接，后用后链接，ex:先libplug.a+再libcom.a+ pthread
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lplug \
	-lcom \
	-L$(TOPDIR)/third_lib/face_detector/mnn/lib/ \
	-lMNN \
	-L$(TOPDIR)/third_lib/opencv_lib/lib/ \
	 -lopencv_core \
	-lopencv_imgproc \
	-lopencv_imgcodecs \
	-pthread 