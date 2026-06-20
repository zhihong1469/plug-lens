# ==========================================================================
# plugins/Makefile：插件模块（条件编译优化版）
# @brief  根据平台配置选择编译哪些插件
# @note   PLATFORM_RK3562=1: 使用RKNN/RGA/MPP硬件加速
#         PLATFORM_IMX6ULL=1: 使用MNN/软件实现
# ==========================================================================

# ===================== 条件编译：AI推理引擎 =====================
ifeq ($(PLATFORM_RK3562),1)
    # RK3562: 使用RKNN NPU硬件加速
    AI_MODEL_SRC := \
        base_plugins/ai_model_rknn/src/ai_model_rknn.cpp
    AI_MODEL_INC := \
        -I$(TOPDIR)/plugins/base_plugins/ai_model_rknn/inc
else
    # i.MX6ULL: 使用MNN CPU推理
    AI_MODEL_SRC := \
        base_plugins/ai_model_mnn/src/ai_model_mnn.cpp \
        base_plugins/ai_model_mnn/src/UltraFaceMNN.cpp
    AI_MODEL_INC := \
        -I$(TOPDIR)/plugins/base_plugins/ai_model_mnn/inc
endif

# ===================== 条件编译：图像处理引擎 =====================
ifeq ($(PLATFORM_RK3562),1)
    # RK3562: 使用RGA硬件加速进行格式转换
    # 但编解码（MJPEG/H.264）和辅助函数仍需要 img_joint 软件实现
    IMG_PROC_SRC := \
        base_plugins/img_rga/src/img_rga.cpp \
        base_plugins/img_joint/src/img_joint.cpp \
        base_plugins/img_joint/src/img_proc_software_ops.cpp
    IMG_PROC_INC := \
        -I$(TOPDIR)/plugins/base_plugins/img_rga/inc \
        -I$(TOPDIR)/plugins/base_plugins/img_joint/inc
else
    # i.MX6ULL: 使用软件实现（libjpeg-turbo、libyuv、openh264等）
    IMG_PROC_SRC := \
        base_plugins/img_joint/src/img_joint.cpp \
        base_plugins/img_joint/src/img_proc_software_ops.cpp
    IMG_PROC_INC := \
        -I$(TOPDIR)/plugins/base_plugins/img_joint/inc
endif

# ===================== 通用插件（所有平台都需要）======================
COMMON_SRC := \
	base_plugins/camera_usb/src/camera_usb.c \
	base_plugins/img_storage/src/img_storage.c \
	base_plugins/led_controller/src/led_indicator.c

COMMON_INC := \
	-I$(TOPDIR)/plugins/base_plugins/camera_usb/inc \
	-I$(TOPDIR)/plugins/base_plugins/img_storage/inc \
	-I$(TOPDIR)/plugins/base_plugins/led_controller/inc

# ===================== 服务插件（所有平台都需要）======================
SERVICE_SRC := \
	service_plugins/net_push_srv/src/rtsp_server.cpp \
	service_plugins/net_push_srv/src/net_push_srv.c \
	service_plugins/capture_srv/src/capture_srv.c  \
	service_plugins/face_detect_srv/src/face_detect_srv.c

SERVICE_INC := \
	-I$(TOPDIR)/plugins/service_plugins/net_push_srv/inc \
	-I$(TOPDIR)/plugins/service_plugins/capture_srv/inc \
	-I$(TOPDIR)/plugins/service_plugins/face_detect_srv/inc

# ===================== 应用插件（所有平台都需要）======================
APP_SRC := \
	app_plugins/src/demo_app.c \
	app_plugins/src/app.c

APP_INC := \
	-I$(TOPDIR)/plugins/app_plugins/inc

# ===================== 合并所有源文件和头文件路径 =====================
SRC_FILES := $(COMMON_SRC) $(AI_MODEL_SRC) $(IMG_PROC_SRC) $(SERVICE_SRC) $(APP_SRC)
LOCAL_INC := $(COMMON_INC) $(AI_MODEL_INC) $(IMG_PROC_INC) $(SERVICE_INC) $(APP_INC)


# 最终路径 = 全局 + 私有
INC_FLAGS := $(GLOBAL_INC) $(LOCAL_INC)

# 	-I$(TOPDIR)/src/fsm/global_fsm/inc \
# 	-I$(TOPDIR)/src/fsm/module_fsm/inc \


# 3. 生成插件静态库
TARGET_NAME := libplug.a
TARGET_TYPE := LIB

# 4. 链接（注意：如果有C++库依赖，需要在这里添加，但我们先不链接，留到最后）
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lcom \
	-pthread \
