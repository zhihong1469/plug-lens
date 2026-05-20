#ifndef __RTSP_SERVER_H__
#define __RTSP_SERVER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  rtsp_server_start(void);
void rtsp_server_push_jpeg(const uint8_t* jpeg_buf, uint32_t jpeg_size);

#ifdef __cplusplus
}
#endif

#endif