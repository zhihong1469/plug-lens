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
# @note   Unified third-party library flags via third_lib/third_lib.mk
#         Platform auto-selection handled in third_lib.mk
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lplug \
	-lcom \
	-pthread \
	$(THIRD_LIB_LDFLAGS) \
	$(PLATFORM_LIBS)