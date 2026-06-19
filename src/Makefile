# ==========================================================================
# src/Makefile
# @author LuoZhihong
# @license MIT License
# ==========================================================================

# 1. 源文件（新增工厂模块）
SRC_FILES := \
	base/camera/src/camera_base.c \
	base/camera/src/camera_factory.c \
 	base/ai_model/src/ai_model_base.cpp \
	base/ai_model/src/ai_model_factory.c \
	base/img_proc/src/img_proc_base.c \
	base/img_proc/src/img_proc_factory.c \
	base/led/src/led_base.c \
	bus/event_bus/src/event_bus.c \
	bus/data_bus/src/data_bus.c \
	app/src/main.c \

# 	fsm/global_fsm/src/global_fsm.c \
# 	fsm/module_fsm/src/module_fsm.c \
	
# 【核心】src 无独有路径，直接用全局
LOCAL_INC :=
INC_FLAGS := $(GLOBAL_INC) $(LOCAL_INC)


# 3. 生成最终可执行文件
TARGET_NAME := vision_ai_app
TARGET_TYPE := EXE

# 4. 链接所有库：必须先用先链接，后用后链接，ex:先libplug.a+再libcom.a+ pthread
# @note   Platform-specific libraries are passed via PLATFORM_LIBS from top-level Makefile
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lplug \
	-lcom \
	-pthread \
	-L$(TOPDIR)/third_lib/aarch64/live555/lib -lliveMedia -lgroupsock -lUsageEnvironment -lBasicUsageEnvironment \
	-L$(TOPDIR)/third_lib/aarch64/openh264/lib -lopenh264 \
	-L$(TOPDIR)/third_lib/aarch64/libyuv/lib -lyuv \
	-L$(TOPDIR)/third_lib/aarch64/libjpeg_turbo/lib -ljpeg -lturbojpeg\
	-L$(TOPDIR)/third_lib/aarch64/face_detector/mnn/lib -lMNN \
	$(PLATFORM_LIBS)