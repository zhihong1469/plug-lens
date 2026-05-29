/* SPDX-License-Identifier: MIT */
#ifndef __CAMERA_BASE_H__
#define __CAMERA_BASE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file    camera_base.h
 * @brief   Camera Device Abstract Base Class (V3.0 Architecture)
 * @details Defines universal camera abstract interfaces, capability structs,
 *          and V4L2 low-level encapsulation.
 *          All camera subclasses (USB/CSI) MUST inherit and implement this base class.
 *          Core Feature: Hardware buffer management with internal closed-loop,
 *          upper layer does NOT need manual release.
 * @version 3.0
 * @author  LuoZhihong
 */

/**
 * @brief container_of macro: V3.0 Forced down-casting solution
 * @details Forbid raw pointer casting, only use this macro for safe base→subclass conversion.
 *          Core macro for embedded C OOP design.
 * @param ptr     Base class pointer
 * @param type    Subclass structure type
 * @param member  Member name of base class in subclass
 */
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

/* Forward declaration: Camera base class */
typedef struct camera_base camera_base_t;

/**
 * @brief Camera hardware capability detection struct
 * @details Universal device capability description, auto-filled during subclass initialization.
 *          Used for upper layer to query supported formats and parameters.
 */
typedef struct {
    char device_name[32];                /**< Device name */
    char bus_info[32];                    /**< Bus information */
    bool support_yuyv;                    /**< Support YUYV format */
    bool support_mjpeg;                   /**< Support MJPEG format */
    bool support_nv12;                    /**< Support NV12 format */
    bool support_exposure;                /**< Support exposure control */
    bool support_white_balance;           /**< Support white balance control */
    bool support_gain;                    /**< Support gain control */
} camera_capability_t;

/**
 * @brief Camera operation function table (OPS)
 * @details V3.0 Mandatory Rules:
 *          1. Only store function pointers, instantiated as const in .c file
 *          2. All subclasses MUST implement ALL interfaces
 *          3. Pure hardware operations, NO business logic
 */
typedef struct {
    /** Device initialization (self-test, format config, buffer allocation) */
    int (*init)(camera_base_t *me);
    /** Device de-initialization (release resources, close device) */
    int (*deinit)(camera_base_t *me);
    /** Start video stream capture */
    int (*start_capture)(camera_base_t *me);
    /** Stop video stream capture */
    int (*stop_capture)(camera_base_t *me);
    /** Get one frame of video data */
    int (*get_frame)(camera_base_t *me, void **frame, size_t *len);
    /** Set camera parameters (exposure/brightness/white balance, etc.) */
    int (*set_param)(camera_base_t *me, int cmd, void *arg);
    /** Get camera hardware capability information */
    int (*get_capability)(camera_base_t *me, camera_capability_t *cap);
} camera_ops_t;

/**
 * @brief Camera base class structure
 * @details V3.0 Mandatory Rules:
 *          1. const ops MUST be the first member
 *          2. Only store public attributes, NO hardware private members
 *          3. Pure device abstraction, NO business logic
 */
struct camera_base {
    const camera_ops_t *ops;  /**< Fixed first position: Read-only operation function table */
    const char *name;         /**< Device name (usb_camera/csi_camera) */
    int width;                /**< Actual effective image width */
    int height;               /**< Actual effective image height */
    uint32_t fps;             /**< Actual effective frame rate */
    bool is_running;          /**< Capture status: true-running false-stopped */
    bool is_init;             /**< Initialization status: true-initialized false-uninitialized */
};

/**
 * @brief Camera public parameter config command enum
 * @details Command words used when calling camera_set_param from upper layer
 */
enum camera_param_cmd {
    CAMERA_PARAM_SET_FPS,             /**< Set frame rate */
    CAMERA_PARAM_SET_EXPOSURE,        /**< Set manual exposure */
    CAMERA_PARAM_SET_BRIGHTNESS,      /**< Set brightness */
    CAMERA_PARAM_SET_WHITE_BALANCE,   /**< Set manual white balance */
    CAMERA_PARAM_SET_GAIN,            /**< Set manual gain */
};

/* ============================================================================
 * @brief V4L2 system call thin encapsulation (Device driver layer only)
 * @details No business logic, only encapsulate Linux V4L2 native interfaces.
 *          Universal for all camera subclasses, FORBID direct upper layer call.
 * ========================================================================== */
/* Basic device operations */
int v4l2_open(const char *dev_path);                /**< Open V4L2 device */
void v4l2_close(int fd);                            /**< Close V4L2 device */
int v4l2_stream_ctrl(int fd, bool on);              /**< Start/Stop video stream */

/* Hardware self-test core functions */
int v4l2_query_capability(int fd, camera_capability_t *cap);  /**< Query device basic capability */
int v4l2_check_control_support(int fd, uint32_t cid);        /**< Check if parameter is supported */
int v4l2_enum_formats(int fd, camera_capability_t *cap);      /**< Enumerate supported pixel formats */

/* Image format configuration */
int v4l2_set_format(int fd, int *width, int *height, uint32_t pixelformat); /**< Set image format */
int v4l2_get_format(int fd, int *width, int *height, uint32_t *pixelformat); /**< Get current format */
int v4l2_set_fps(int fd, uint32_t *fps);                                    /**< Set and read back FPS */

/* Buffer management */
int v4l2_reqbufs(int fd, int *buf_cnt);                 /**< Request V4L2 kernel buffers */
int v4l2_querybuf(int fd, struct v4l2_buffer *buf);     /**< Query buffer information */
int v4l2_qbuf(int fd, struct v4l2_buffer *buf);        /**< Enqueue buffer (return to kernel) */
int v4l2_dqbuf(int fd, struct v4l2_buffer *buf);       /**< Dequeue buffer (get from kernel) */
void *v4l2_mmap(int fd, size_t length, off_t offset);  /**< Memory map */
void v4l2_munmap(void *addr, size_t length);           /**< Unmap memory */

/* Parameter control */
int v4l2_set_ctrl(int fd, uint32_t cid, int value);    /**< Set V4L2 control parameter */
int v4l2_get_ctrl(int fd, uint32_t cid, int *value);  /**< Get V4L2 control parameter */

/* ============================================================================
 * @brief Camera base class unified external interfaces (V3.0 Forced check + dispatch)
 * @details The ONLY call interfaces for upper business modules,
 *          auto parameter check + dispatch to subclass implementation.
 * ========================================================================== */
/**
 * @brief  Initialize camera device
 * @param  me  Camera base class pointer
 * @return 0=success, negative=failure
 */
int camera_init(camera_base_t *me);

/**
 * @brief  De-initialize camera device
 * @param  me  Camera base class pointer
 * @return 0=success, negative=failure
 */
int camera_deinit(camera_base_t *me);

/**
 * @brief  Start camera capture
 * @param  me  Camera base class pointer
 * @return 0=success, negative=failure
 */
int camera_start_capture(camera_base_t *me);

/**
 * @brief  Stop camera capture
 * @param  me  Camera base class pointer
 * @return 0=success, negative=failure
 */
int camera_stop_capture(camera_base_t *me);

/**
 * @brief  Get one frame of video data
 * @param  me     Camera base class pointer
 * @param frame   Output: frame data pointer
 * @param len     Output: frame data length
 * @return 0=success, negative=failure
 * @note   Core Important: V4L2 buffer dqbuf+qbuf closed-loop implemented internally.
 *         Hardware buffer auto-recycled, upper layer **MUST NOT** release manually!
 */
int camera_get_frame(camera_base_t *me, void **frame, size_t *len);

/**
 * @brief  Set camera parameters
 * @param  me   Camera base class pointer
 * @param  cmd  Parameter command (enum camera_param_cmd)
 * @param  arg  Parameter value pointer
 * @return 0=success, negative=failure
 */
int camera_set_param(camera_base_t *me, int cmd, void *arg);

/**
 * @brief  Get camera hardware capability information
 * @param  me   Camera base class pointer
 * @param  cap  Output: capability struct
 * @return 0=success, negative=failure
 */
int camera_get_capability(camera_base_t *me, camera_capability_t *cap);

#ifdef __cplusplus
}
#endif

#endif /* __CAMERA_BASE_H__ */