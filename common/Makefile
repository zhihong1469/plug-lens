# ==========================================================================
# common/Makefile：公共组件库
# ==========================================================================

# 1. 源文件（相对于 common/ 目录，直接写子路径）
SRC_FILES := \
	log/src/log.c \
	queue/src/queue.c \
	plugin/src/plugin_loader.c \
	pool/src/pool.c \
	utils/src/utils.c \
	thread/src/thread.c \
	img_proc/src/img_proc_c.c \
	initcall/src/initcall.c \
	../third_lib/tlsf-master/tlsf.c \
	mem_adapter/src/mem_adapter.c \
	daemon/src/daemon.c \
	sd_mount/src/sd_mount.c \
	sys_time/src/sys_time_sync.c \
	network/src/network_check.c \

# 模块私有路径（common 无独有路径，留空）
LOCAL_INC :=

# 最终编译路径 = 全局公共路径 + 模块私有路径
INC_FLAGS := $(GLOBAL_INC) $(LOCAL_INC)


# 3. 生成静态库
TARGET_NAME := libcom.a
TARGET_TYPE := LIB

LDFLAGS := \
	-pthread \