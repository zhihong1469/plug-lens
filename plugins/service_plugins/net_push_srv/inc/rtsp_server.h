/**
 * @file    rtsp_server.h
 * @brief   RTSP Server Public Interface (Live555 Based + DataBus V4.0 H.264 Pull)
 * @details RTSP server for i.MX6ULL, supports H.264 streaming from dedicated DataBus,
 *          client connection management, thread-safe C interface for C/C++ integration.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @relies  http://www.live555.com/liveMedia/
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */

#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Set H.264 SPS/PPS parameters (MUST call before RTSP start)
 * @param   sps_pps  Pointer to SPS+PPS combined data
 * @param   len      Length of SPS+PPS data
 * @return  None
 */
void rtsp_set_sps_pps(const uint8_t* sps_pps, uint32_t len);

/**
 * @brief   Start RTSP service independently
 * @return  0 on success, negative value on failure
 */
int rtsp_start_service(void);

/**
 * @brief   Check if RTSP server is running
 * @return  true = running, false = stopped
 */
bool rtsp_is_running(void);

/**
 * @brief   Push frame data to RTSP (Legacy interface, DataBus is used now)
 * @param   buf   Frame data pointer
 * @param   size  Frame data length
 * @return  None
 */
void rtsp_server_push(const uint8_t* buf, uint32_t size);

/**
 * @brief   Check if any RTSP client is connected
 * @return  true = client connected, false = no clients
 */
bool rtsp_has_clients(void);

/**
 * @brief   Stop RTSP server and release all resources
 * @return  0 on success
 */
int rtsp_server_stop(void);

/**
 * @brief   Legacy compatibility interface (no actual function)
 * @return  0 always
 */
int rtsp_server_start(void);

#ifdef __cplusplus
}
#endif

#endif // RTSP_SERVER_H