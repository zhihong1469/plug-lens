/**
 * @file    camera_usb.h
 * @brief   USB Camera Subclass Driver (V3.0 Encapsulated)
 * @details Inherits from camera_base class, implements V4L2-based USB camera driver.
 *          Fully encapsulated private implementation, only 2 standard public interfaces exposed.
 *          Hardware buffer management is internal, upper layer does not care about low-level details.
 *          Optimized for embedded Linux (IMX6ULL) platform.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All functions are NOT thread-safe.
 *          2. Call sequence: create → init → start_capture → get_frame → stop_capture → deinit → destroy.
 *          3. Device path format: /dev/video0, /dev/video1, etc.
 */
#ifndef __CAMERA_USB_H__
#define __CAMERA_USB_H__

#include "camera_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Create USB camera instance (public constructor)
 * @details Allocate subclass memory, initialize base class and hardware parameters.
 *          Low-level initialization is executed in the init() virtual function.
 * @param   dev_path    Linux V4L2 device node path (e.g., /dev/video0)
 * @param   width       Desired image width
 * @param   height      Desired image height
 * @param   fmt         Pixel format (V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_MJPEG, etc.)
 * @param   fps         Desired frame rate
 * @return  camera_base_t* Pointer to camera base handle on success; NULL on failure
 *
 * @pre     None (first interface to call)
 * @post    Camera instance allocated, in IDLE state (not initialized)
 * @warning Do not call capture APIs before initialization
 * @thread_safety No
 */
camera_base_t *camera_usb_create(const char *dev_path,
                                 int width,
                                 int height,
                                 uint32_t fmt,
                                 uint32_t fps);

/**
 * @brief   Destroy USB camera instance (public destructor)
 * @details Auto execution flow: stop capture → deinitialize → release memory.
 *          Set pointer to NULL after calling to avoid dangling pointer.
 * @param   base_me     Pointer to camera base handle
 * @return  void
 *
 * @pre     Instance must be created by camera_usb_create
 * @post    All hardware/software resources released, handle invalid
 * @note    Must be called to prevent resource leaks
 * @thread_safety No
 */
void camera_usb_destroy(camera_base_t *base_me);

#ifdef __cplusplus
}
#endif

#endif