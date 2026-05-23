# ==========================================================================
# plugins/Makefile：插件模块（新增AI相关）
# ==========================================================================

# 1. 源文件
SRC_FILES := \
	base_plugins/camera_usb/src/camera_usb.c \
	base_plugins/ai_model_mnn/src/ai_model_mnn.cpp \
	base_plugins/ai_model_mnn/src/UltraFaceMNN.cpp \
	base_plugins/sd_storage/src/sd_storage.c \
	base_plugins/img_joint/src/img_joint.cpp \
	service_plugins/net_push_srv/src/rtsp_server.cpp \
	service_plugins/net_push_srv/src/net_push_srv.c \
	service_plugins/capture_srv/src/capture_srv.c  \
	service_plugins/face_detect_srv/src/face_detect_srv.c \
	app_plugins/src/demo_app.c \
	app_plugins/src/app.c
# 	app_plugins/src/net_demo.c \



# 仅定义 plugins 独有的私有路径！全局路径自动继承
LOCAL_INC := \
	-I$(TOPDIR)/plugins/base_plugins/camera_usb/inc \
	-I$(TOPDIR)/plugins/base_plugins/ai_model_mnn/inc \
	-I$(TOPDIR)/plugins/base_plugins/sd_storage/inc \
	-I$(TOPDIR)/plugins/service_plugins/net_push_srv/inc \
	-I$(TOPDIR)/plugins/service_plugins/capture_srv/inc \
	-I$(TOPDIR)/plugins/service_plugins/face_detect_srv/inc \
	-I$(TOPDIR)/plugins/app_plugins/inc \
	-I$(TOPDIR)/plugins/base_plugins/img_joint/inc
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
