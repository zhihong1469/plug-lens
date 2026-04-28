# ==========================================================================
# app/Makefile：应用层
# ==========================================================================

# 1. 源文件（相对app目录：main/main.c，后续加直接追加）
SRC_FILES := \
	main/main.c \
	modules/v4l2_app/src/v4l2_app.c

# 2. 头文件路径（包含BSP的头文件）
INC_FLAGS := \
	-I$(TOPDIR)/BSP/common/log/inc \
	-I$(TOPDIR)/BSP/common/queue/inc \
	-I$(TOPDIR)/BSP/v4l2_video/inc \
	-I$(TOPDIR)/app/modules/v4l2_app/inc

# 3. 生成可执行文件
TARGET_NAME := vision_ai_app
TARGET_TYPE := EXE

# 4. 链接 libbsp.a
LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lbsp \
	-pthread