# ==========================================================================
# common/Makefile
# @brief  Common component static library build configuration
# @details Build libcom.a (all common modules: log, queue, thread, utils, etc.)
# @author LuoZhihong
# @license MIT License
# ==========================================================================

# ===================== Source Files Configuration =====================
# @brief  Source files (relative path to common/ directory)
# @note   Include all core common modules and third-party TLSF memory allocator
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

# ===================== Include Path Configuration =====================
# @brief  Local private include paths (empty for common module)
LOCAL_INC :=

# @brief  Final include flags (global common includes + local private includes)
INC_FLAGS := $(GLOBAL_INC) $(LOCAL_INC)

# ===================== Build Target Configuration =====================
# @brief  Output static library name
TARGET_NAME := libcom.a
# @brief  Build target type (LIB = static library)
TARGET_TYPE := LIB

# ===================== Linker Flags =====================
# @brief  pthread library for multi-thread support
LDFLAGS := \
	-pthread \