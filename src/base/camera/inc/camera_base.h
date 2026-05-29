/* SPDX-License-Identifier: MIT */
/**
 * @file    camera_base.h
 * @brief   Camera Device Abstract Base Class (V3.0 Architecture)
 * @details Universal abstract interface for camera devices (USB/CSI),
 *          V4L2 kernel interface low-level encapsulation, C-OOP polymorphic design.
 *          Core feature: Hardware buffer closed-loop management, auto-recycle without manual release.
 *          All camera subclasses MUST inherit and implement this base class.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All functions are not thread-safe unless marked specially.
 *          2. Call functions in order: init → start_capture → get_frame → stop_capture → deinit.
 *          3. Subclasses must place camera_base_t as the first structure member.
 *          4. V4L2 buffer operations are handled internally, upper layer MUST NOT modify buffers.
 */

/**
 * @brief   Safe type conversion macro for C-OOP
 * @details Forced base-to-subclass casting solution, forbid raw pointer casting.
 *          Core macro for embedded object-oriented programming.
 *
 * @param   ptr     Pointer to base class instance
 * @param   type    Data type of target subclass structure
 * @param   member  Name of base class member in subclass structure
 * @return  Valid pointer to subclass instance
 */
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

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

/**
 * @brief   Forward declaration of camera base class
 * @details Opaque structure, external modules only use the pointer type
 */
typedef struct camera_base camera_base_t;

/**
 * @brief   Camera hardware capability structure
 * @details Stores device information and supported features, auto-filled during initialization.
 *          Used by upper layers to query device capabilities and supported formats.
 */
typedef struct {
    char device_name[32];                /**< Camera device name string */
    char bus_info[32];                    /**< Hardware bus information (USB/CSI) */
    bool support_yuyv;                    /**< Support YUYV pixel format */
    bool support_mjpeg;                   /**< Support MJPEG compressed format */
    bool support_nv12;                    /**< Support NV12 semi-planar format */
    bool support_exposure;                /**< Support manual exposure control */
    bool support_white_balance;           /**< Support white balance adjustment */
    bool support_gain;                    /**< Support analog/digital gain control */
} camera_capability_t;

/**
 * @brief   Camera operation virtual function table (OPS)
 * @details Polymorphic interface for subclasses, pure hardware operations only.
 * @note    V3.0 Mandatory Rules:
 *          1. Must be instantiated as const in source file
 *          2. All subclasses MUST implement all function pointers
 *          3. No business logic, only hardware register/driver operations
 */
typedef struct {
    int (*init)(camera_base_t *me);            /**< Initialize hardware and resources */
    int (*deinit)(camera_base_t *me);          /**< Release hardware and resources */
    int (*start_capture)(camera_base_t *me);   /**< Start video stream capture */
    int (*stop_capture)(camera_base_t *me);    /**< Stop video stream capture */
    int (*get_frame)(camera_base_t *me, void **frame, size_t *len); /**< Get video frame data */
    int (*set_param)(camera_base_t *me, int cmd, void *arg); /**< Set camera parameters */
    int (*get_capability)(camera_base_t *me, camera_capability_t *cap); /**< Get device capabilities */
} camera_ops_t;

/**
 * @brief   Camera base class core structure
 * @details Base object for all camera devices, public attributes only.
 * @note    V3.0 Mandatory Rules:
 *          1. const ops table MUST be the FIRST member
 *          2. No hardware-specific private members
 *          3. Pure device abstraction, no business logic
 *          4. Do NOT modify members directly, use public APIs only
 */
struct camera_base {
    const camera_ops_t *ops;      /**< Read-only virtual function table (fixed first position) */
    const char *name;             /**< Device identifier (usb_camera/csi_camera) */
    int width;                    /**< Effective image width (pixels) */
    int height;                   /**< Effective image height (pixels) */
    uint32_t fps;                 /**< Actual frame rate (frames per second) */
    bool is_running;              /**< Stream status: true = capturing, false = stopped */
    bool is_init;                 /**< Init status: true = initialized, false = uninitialized */
};

/**
 * @brief   Camera parameter control commands
 * @details Command enumeration for camera_set_param() interface,
 *          used by upper layers to configure camera parameters.
 */
enum camera_param_cmd {
    CAMERA_PARAM_SET_FPS,             /**< Set video frame rate */
    CAMERA_PARAM_SET_EXPOSURE,        /**< Set manual exposure value */
    CAMERA_PARAM_SET_BRIGHTNESS,      /**< Set image brightness */
    CAMERA_PARAM_SET_WHITE_BALANCE,   /**< Set manual white balance */
    CAMERA_PARAM_SET_GAIN,            /**< Set sensor gain value */
};

/* ============================================================================
 * @brief   V4L2 kernel system call thin encapsulation
 * @details Low-level driver layer only, no business logic, universal for all subclasses.
 *          FORBID direct calls from upper business modules.
 * ========================================================================== */
/**
 * @brief   Open V4L2 video device
 * @param   dev_path    Device file path (e.g. /dev/video0)
 * @return  File descriptor (>=0) on success, negative errno on failure
 * @pre     Device path must be valid and accessible
 * @thread_safety No
 */
int v4l2_open(const char *dev_path);

/**
 * @brief   Close V4L2 video device
 * @param   fd  V4L2 device file descriptor
 * @return  None
 * @pre     File descriptor must be valid
 * @thread_safety No
 */
void v4l2_close(int fd);

/**
 * @brief   Control V4L2 stream start/stop
 * @param   fd  V4L2 device file descriptor
 * @param   on  true = start stream, false = stop stream
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_stream_ctrl(int fd, bool on);

/**
 * @brief   Query V4L2 device basic capabilities
 * @param   fd  V4L2 device file descriptor
 * @param   cap Output pointer to store capability data
 * @return  0 on success, negative errno on failure
 * @pre     File descriptor and capability pointer must be valid
 * @thread_safety No
 */
int v4l2_query_capability(int fd, camera_capability_t *cap);

/**
 * @brief   Check if V4L2 control parameter is supported
 * @param   fd  V4L2 device file descriptor
 * @param   cid V4L2 control ID
 * @return  1 = supported, 0 = not supported
 * @thread_safety No
 */
int v4l2_check_control_support(int fd, uint32_t cid);

/**
 * @brief   Enumerate supported pixel formats and controls
 * @param   fd  V4L2 device file descriptor
 * @param   cap Output pointer to store format support info
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_enum_formats(int fd, camera_capability_t *cap);

/**
 * @brief   Set V4L2 video format and verify actual parameters
 * @param   fd            V4L2 device file descriptor
 * @param   width         Input: target width, Output: actual width
 * @param   height        Input: target height, Output: actual height
 * @param   pixelformat   V4L2 pixel format FourCC code
 * @return  0 on success, negative errno on failure
 * @note    Auto read-back to confirm hardware-supported resolution
 * @thread_safety No
 */
int v4l2_set_format(int fd, int *width, int *height, uint32_t pixelformat);

/**
 * @brief   Get current V4L2 video format
 * @param   fd            V4L2 device file descriptor
 * @param   width         Output: image width
 * @param   height        Output: image height
 * @param   pixelformat   Output: pixel format FourCC code
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_get_format(int fd, int *width, int *height, uint32_t *pixelformat);

/**
 * @brief   Set V4L2 frame rate and verify actual value
 * @param   fd  V4L2 device file descriptor
 * @param   fps Input: target FPS, Output: actual FPS
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_set_fps(int fd, uint32_t *fps);

/**
 * @brief   Request V4L2 kernel buffers for streaming
 * @param   fd      V4L2 device file descriptor
 * @param   buf_cnt Input: request count, Output: actual allocated count
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_reqbufs(int fd, int *buf_cnt);

/**
 * @brief   Query V4L2 buffer information
 * @param   fd  V4L2 device file descriptor
 * @param   buf V4L2 buffer structure pointer
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_querybuf(int fd, struct v4l2_buffer *buf);

/**
 * @brief   Enqueue buffer to V4L2 kernel queue
 * @param   fd  V4L2 device file descriptor
 * @param   buf V4L2 buffer structure pointer
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_qbuf(int fd, struct v4l2_buffer *buf);

/**
 * @brief   Dequeue buffer from V4L2 kernel queue
 * @param   fd  V4L2 device file descriptor
 * @param   buf V4L2 buffer structure pointer
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_dqbuf(int fd, struct v4l2_buffer *buf);

/**
 * @brief   Memory map V4L2 kernel buffer to user space
 * @param   fd      V4L2 device file descriptor
 * @param   length  Buffer length
 * @param   offset  Buffer offset
 * @return  Mapped virtual address on success, MAP_FAILED on failure
 * @thread_safety No
 */
void *v4l2_mmap(int fd, size_t length, off_t offset);

/**
 * @brief   Unmap user-space memory buffer
 * @param   addr    Mapped virtual address
 * @param   length  Buffer length
 * @return  None
 * @thread_safety No
 */
void v4l2_munmap(void *addr, size_t length);

/**
 * @brief   Set V4L2 device control parameter
 * @param   fd      V4L2 device file descriptor
 * @param   cid     V4L2 control ID
 * @param   value   Target parameter value
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_set_ctrl(int fd, uint32_t cid, int value);

/**
 * @brief   Get V4L2 device control parameter
 * @param   fd      V4L2 device file descriptor
 * @param   cid     V4L2 control ID
 * @param   value   Output pointer to store parameter value
 * @return  0 on success, negative errno on failure
 * @thread_safety No
 */
int v4l2_get_ctrl(int fd, uint32_t cid, int *value);

/* ============================================================================
 * @brief   Camera base class unified public interfaces
 * @details ONLY valid interfaces for upper business modules,
 *          Auto parameter validation + polymorphic dispatch to subclasses.
 * ========================================================================== */
/**
 * @brief   Initialize camera base class and subclass hardware
 * @param   me  Pointer to camera base class instance, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be in uninitialized state
 * @post    Instance marked as initialized if success
 *
 * @warning Do not call repeatedly
 * @thread_safety No
 *
 * @example
 * @code
 * camera_base_t *cam = usb_camera_create("/dev/video0");
 * int ret = camera_init(cam);
 * if (ret != 0) {
 *     // Handle initialization error
 * }
 * @endcode
 */
int camera_init(camera_base_t *me);

/**
 * @brief   De-initialize camera and release all resources
 * @param   me  Pointer to camera base class instance, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Capture must be stopped before deinit
 * @post    Instance marked as uninitialized, all resources released
 *
 * @thread_safety No
 */
int camera_deinit(camera_base_t *me);

/**
 * @brief   Start camera video capture stream
 * @param   me  Pointer to camera base class instance, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be initialized
 * @post    Capture state set to running
 *
 * @thread_safety No
 */
int camera_start_capture(camera_base_t *me);

/**
 * @brief   Stop camera video capture stream
 * @param   me  Pointer to camera base class instance, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be running
 * @post    Capture state set to stopped
 *
 * @thread_safety No
 */
int camera_stop_capture(camera_base_t *me);

/**
 * @brief   Get one valid video frame from camera
 * @param   me      Pointer to camera base class instance, cannot be NULL
 * @param   frame   Output pointer to frame data buffer, cannot be NULL
 * @param   len     Output pointer to frame data length, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Capture must be started
 * @post    Buffer auto-enqueued internally (closed-loop management)
 *
 * @note    Core feature: V4L2 DQBUF/QBUF handled internally,
 *          Upper layer MUST NOT release or modify the frame buffer
 * @warning Do not free the returned frame pointer
 * @thread_safety No
 */
int camera_get_frame(camera_base_t *me, void **frame, size_t *len);

/**
 * @brief   Set camera runtime parameters
 * @param   me  Pointer to camera base class instance, cannot be NULL
 * @param   cmd Parameter control command (enum camera_param_cmd)
 * @param   arg Pointer to parameter value, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be initialized
 * @thread_safety No
 */
int camera_set_param(camera_base_t *me, int cmd, void *arg);

/**
 * @brief   Get camera hardware capability information
 * @param   me  Pointer to camera base class instance, cannot be NULL
 * @param   cap Output pointer to capability structure, cannot be NULL
 * @return  0 on success, negative errno on failure
 *
 * @pre     Instance must be initialized
 * @thread_safety No
 */
int camera_get_capability(camera_base_t *me, camera_capability_t *cap);

#ifdef __cplusplus
}
#endif

#endif /* __CAMERA_BASE_H__ */