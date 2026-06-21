#include "camera_factory.h"
#include "board_option.h"

#if CAMERA_INTERFACE_USB
#include "camera_usb.h"
#elif CAMERA_INTERFACE_CSI
#include "camera_csi.h"
#endif

camera_base_t *camera_factory_create(const camera_config_t *config)
{
    if (!config) {
        return NULL;
    }

#if CAMERA_INTERFACE_USB
    /* USB camera create signature: (dev_path, width, height, fmt, fps) */
    return camera_usb_create(config->dev_path, 
                             config->width, 
                             config->height, 
                             config->format,  /* fmt parameter */
                             config->fps);
#elif CAMERA_INTERFACE_CSI
    /* CSI camera create signature: TBD - adapt when implemented */
    return camera_csi_create(config->dev_path, 
                             config->width, 
                             config->height, 
                             config->format, 
                             config->fps,
                             config->buf_count);
#else
    return NULL;
#endif
}

void camera_factory_destroy(camera_base_t *handle)
{
    if (!handle) {
        return;
    }

    if (handle->ops->deinit) {
        handle->ops->deinit(handle);
    }

#if CAMERA_INTERFACE_USB
    camera_usb_destroy(handle);
#elif CAMERA_INTERFACE_CSI
    camera_csi_destroy(handle);
#endif
}