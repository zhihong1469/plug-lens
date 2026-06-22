#ifndef __CAMERA_FACTORY_H__
#define __CAMERA_FACTORY_H__

#include "camera_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Camera configuration structure for factory creation
 * @details Contains all parameters needed to create a camera instance
 */
typedef struct {
    const char *dev_path;    /**< Camera device path (e.g. /dev/video0) */
    int width;               /**< Video frame width in pixels */
    int height;              /**< Video frame height in pixels */
    uint32_t fps;            /**< Desired frame rate (frames per second) */
    uint32_t format;         /**< V4L2 pixel format (V4L2_PIX_FMT_xxx) */
    uint32_t buf_count;      /**< Number of internal buffers */
} camera_config_t;

/**
 * @brief   Create camera instance based on platform configuration
 * @details Automatically selects the appropriate camera interface (USB/CSI)
 *          based on the BOARD_OPTION_H settings.
 * @param   config  Pointer to camera configuration structure
 * @return  Valid camera handle on success, NULL on failure
 * 
 * @pre     config must not be NULL and must contain valid parameters
 * @post    Handle is allocated but camera is NOT initialized (call init() to initialize)
 * @note    Factory pattern hides implementation details from caller
 * @warning Caller is responsible for destroying the handle when done
 * 
 * @example
 * @code
 * camera_config_t config = {
 *     .dev_path = "/dev/video0",
 *     .width = 640,
 *     .height = 360,
 *     .fps = 15,
 *     .format = V4L2_PIX_FMT_YUYV,
 *     .buf_count = 4
 * };
 * camera_base_t *cam = camera_factory_create(&config);
 * if (cam) {
 *     cam->ops->init(cam);
 *     cam->ops->start_capture(cam);
 *     // ... use camera ...
 *     cam->ops->stop_capture(cam);
 *     camera_factory_destroy(cam);
 * }
 * @endcode
 */
camera_base_t *camera_factory_create(const camera_config_t *config);

/**
 * @brief   Destroy camera instance
 * @param   handle  Camera handle to destroy
 * @note    Safe to call with NULL handle
 */
void camera_factory_destroy(camera_base_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* __CAMERA_FACTORY_H__ */