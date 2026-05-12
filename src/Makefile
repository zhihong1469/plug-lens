# ==========================================================================
# src/Makefile：核心框架模块（新增AI Link路径）
# ==========================================================================

# 1. 源文件（新增 ai_model_link.cpp）
SRC_FILES := \
	link/frame_link/src/frame_link.c \
	base/camera/src/camera_base.c \
 	base/ai_model/src/ai_model_base.cpp \
	base/service/src/service_base.c \
	bus/event_bus/src/event_bus.c \
	bus/data_bus/src/data_bus.c 

# 	fsm/global_fsm/src/global_fsm.c \
# 	fsm/module_fsm/src/module_fsm.c \
	
# 【核心】src 无独有路径，直接用全局
LOCAL_INC :=
INC_FLAGS := $(GLOBAL_INC) $(LOCAL_INC)


# 3. 生成最终可执行文件
TARGET_NAME := vision_ai_app
TARGET_TYPE := EXE

# 4. 链接所有库：必须先用先链接，后用后链接，ex:先libplug.a+再libcom.a+ pthread
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lplug \
	-lcom \
	-pthread \
	-L$(TOPDIR)/third_lib/face_detector/mnn/lib/ \
	-lMNN \
	-L$(TOPDIR)/third_lib/opencv_lib/lib/ \
	-lopencv_core \
	-lopencv_imgproc \
	-lopencv_imgcodecs 

