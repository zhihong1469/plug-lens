# ==========================================================================
# BSP/Makefile：硬件抽象层（V4L2 + 公共工具）
# ==========================================================================

# 1. 源文件（所有BSP子模块的.c文件，相对BSP目录）
SRC_FILES := \
	common/log/src/log.c \
	common/queue/src/queue.c \
	v4l2_video/src/v4l2_video.c 

# 2. 头文件路径（必须包含所有BSP的inc目录）
INC_FLAGS := \
	-I$(SRC_DIR)/common/log/inc \
	-I$(SRC_DIR)/common/queue/inc \
	-I$(SRC_DIR)/v4l2_video/inc 
	

# 3. 编译为静态库 libbsp.a
TARGET_NAME := libbsp.a
TARGET_TYPE := LIB

LDFLAGS :=