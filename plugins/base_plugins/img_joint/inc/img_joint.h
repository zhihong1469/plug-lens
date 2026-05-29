/**
 * @file    img_joint.h
 * @brief   Universal Image Joint Processing Module
 * @details Cross-module image utility functions for format conversion, encoding/decoding.
 *          Powered by libyuv, TurboJPEG and OpenH264; No OpenCV dependency.
 *          Optimized for IMX6ULL embedded Linux platform, supports C/C++ calling.
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @relies  https://github.com/lemenkov/libyuv
 *          https://github.com/cisco/openh264
 *          https://github.com/libjpeg-turbo/libjpeg-turbo
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    Global rules:
 *          1. All functions are thread-safe only if using independent handles.
 *          2. Image width/height must be even numbers for H.264 encoding.
 *          3. IMX6ULL requires single-core encoding mode (use_cpu_core = true).
 */
#ifndef __IMG_JOINT_H__
#define __IMG_JOINT_H__

#include <stdint.h>
#include <stdbool.h>
#include "mem_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

// ========================== Error Code Definition ==========================
/**
 * @brief   Image joint module return codes
 * @details Aligned with project global error code standard
 */
typedef enum {
    IMG_JOINT_OK         =  0,   /**< Operation succeeded */
    IMG_JOINT_ERR_INPUT  = -1,   /**< Invalid input parameter */
    IMG_JOINT_ERR_SKIP   = -2,   /**< Frame skip (normal optimization) */
    IMG_JOINT_ERR_JPEG   = -5,   /**< JPEG encode/decode failure */
    IMG_JOINT_ERR_H264   = -6    /**< H264 encode failure */
} ImgJoint_Error_t;

// ========================== H264 Encoder Handle ==========================
/**
 * @brief   Opaque handle for H264 encoder
 * @details External code uses this pointer only; internal implementation hidden.
 */
typedef void* h264_encoder_t;

// ========================== H264 Encoder Configuration ==========================
/**
 * @brief   H264 encoding parameters (Optimized for IMX6ULL real-time streaming)
 * @details Critical parameters for embedded video encoding, all values validated.
 */
typedef struct {
    int     width;          /**< Input image width (MUST be even number) */
    int     height;         /**< Input image height (MUST be even number) */
    int     fps;            /**< Frame rate (15~30 recommended for IMX6ULL) */
    int     bitrate;        /**< Bitrate (kbps, 300~500 for 320*240) */
    int     gop;            /**< GOP size (I-frame interval, recommended 15) */
    bool    use_cpu_core;   /**< Single-core encoding flag (MUST be true for IMX6ULL) */
} h264_encode_param_t;

// ========================== Image Format Conversion APIs ==========================
/**
 * @brief   Convert YUYV (YUV422) to RGB888
 * @details NEON-accelerated by libyuv, high performance for embedded systems.
 * @param   yuyv_data   Input YUYV data buffer
 * @param   width       Image width (even number required)
 * @param   height      Image height
 * @param   rgb_buf     Output RGB888 buffer (size >= width*height*3)
 * @return  ImgJoint_Error_t  Status code
 *
 * @pre     Input buffers must be valid and non-NULL
 * @post    RGB888 data stored in output buffer
 * @thread_safety No
 */
int yuyv_to_rgb(const uint8_t* yuyv_data, int width, int height, uint8_t* rgb_buf);

/**
 * @brief   Decode MJPEG to RGB888
 * @details High-performance hardware decoding via TurboJPEG.
 * @param   mjpeg_data  Input MJPEG compressed data
 * @param   data_len    Length of MJPEG data (bytes)
 * @param   width       Decoded image width
 * @param   height      Decoded image height
 * @param   rgb_buf     Output RGB888 buffer
 * @return  ImgJoint_Error_t  Status code
 *
 * @pre     Input buffers must be valid
 * @post    Decompressed RGB data stored in output buffer
 * @thread_safety No
 */
int mjpeg_to_rgb(const uint8_t* mjpeg_data, int data_len, int width, int height, uint8_t* rgb_buf);

/**
 * @brief   Convert YUYV (YUV422) to I420 (YUV420P)
 * @details NEON-accelerated, dedicated for H.264 encoding.
 * @param   yuyv_data   Input YUYV data buffer
 * @param   width       Image width (even number required)
 * @param   height      Image height (even number required)
 * @param   i420_buf    Output I420 buffer (size >= width*height*3/2)
 * @return  ImgJoint_Error_t  Status code
 *
 * @pre     Input parameters valid, width/height even
 * @post    I420 format data ready for H264 encoding
 * @thread_safety No
 */
int yuyv_to_i420(const uint8_t* yuyv_data, int width, int height, uint8_t* i420_buf);

/**
 * @brief   Convert I420 (YUV420P) to RGB888
 * @details NEON-accelerated by libyuv.
 * @param   i420_data   Input I420 data buffer
 * @param   width       Image width
 * @param   height      Image height
 * @param   rgb_buf     Output RGB888 buffer
 * @return  ImgJoint_Error_t  Status code
 *
 * @pre     Input buffers valid
 * @thread_safety No
 */
int i420_to_rgb(const uint8_t* i420_data, int width, int height, uint8_t* rgb_buf);

/**
 * @brief   RGB888 image scaling
 * @details Bilinear filtering via libyuv, replaces OpenCV resize.
 * @param   src_rgb     Source RGB888 image
 * @param   src_w       Source width
 * @param   src_h       Source height
 * @param   dst_rgb     Destination RGB888 image
 * @param   dst_w       Target width
 * @param   dst_h       Target height
 * @return  ImgJoint_Error_t  Status code
 *
 * @pre     All buffers allocated with sufficient size
 * @post    Scaled image stored in destination buffer
 * @thread_safety No
 */
int rgb_resize(const uint8_t* src_rgb, int src_w, int src_h,
               uint8_t* dst_rgb, int dst_w, int dst_h);

/**
 * @brief   AI preprocessing: Resize and center crop (OpenCV compatible)
 * @details Standard AI model input processing, 640x360 -> 160x120.
 * @param   src_bgr     Source BGR888 image
 * @param   src_w       Source width
 * @param   src_h       Source height
 * @param   dst_bgr     Destination BGR888 image
 * @param   dst_w       Target width
 * @param   dst_h       Target height
 * @return  ImgJoint_Error_t  Status code
 *
 * @thread_safety No
 */
int image_resize_ai_opencv(const uint8_t* src_bgr, int src_w, int src_h,
                           uint8_t* dst_bgr, int dst_w, int dst_h);

/**
 * @brief   Draw rectangle on BGR image
 * @details Raw pointer operation, no third-party library dependency.
 * @param   rgb_data    BGR image buffer
 * @param   width       Image width
 * @param   height      Image height
 * @param   x           Top-left X coordinate
 * @param   y           Top-left Y coordinate
 * @param   w           Rectangle width
 * @param   h           Rectangle height
 * @param   color       RGB color (0xFF0000=Red, 0x00FF00=Green, 0x0000FF=Blue)
 * @param   thickness   Border thickness
 * @return  void
 *
 * @thread_safety No
 */
void bgr_draw_rect(uint8_t* rgb_data, int width, int height,
                   int x, int y, int w, int h, uint32_t color, int thickness);

// ========================== H264 Encoder APIs ==========================
/**
 * @brief   Create H264 encoder instance
 * @details Initialize OpenH264 encoder, optimized for IMX6ULL single-core.
 * @param   param   Pointer to H264 encoding parameters
 * @return  h264_encoder_t  Encoder handle on success; NULL on failure
 *
 * @pre     Parameter struct fully configured
 * @post    Encoder ready for frame encoding
 * @warning IMX6ULL MUST set use_cpu_core = true
 * @thread_safety No
 * @example Refer to the demo code in header file
 */
h264_encoder_t h264_encoder_create(const h264_encode_param_t* param);

/**
 * @brief   Encode YUYV frame to H264 stream
 * @details Auto-convert YUYV to I420 before encoding, IMX6ULL optimized.
 * @param   encoder       Encoder handle
 * @param   yuyv_data     Input YUYV raw data
 * @param   yuyv_len      Length of YUYV data buffer
 * @param   out_h264      Output H264 bitstream buffer
 * @param   out_h264_len  Input: Max buffer size; Output: Actual encoded length
 * @return  ImgJoint_Error_t  Status code
 *
 * @pre     Encoder created successfully
 * @post    H264 data written to output buffer
 * @thread_safety No
 */
int yuyv_to_h264(h264_encoder_t encoder,
                 const uint8_t* yuyv_data,
                 int yuyv_len,
                 uint8_t* out_h264,
                 int* out_h264_len);

/**
 * @brief   Get H264 SPS/PPS header
 * @details Standard official encoder interface, required for RTSP streaming.
 * @param   encoder     Encoder handle
 * @param   sps_pps_buf Output buffer for SPS/PPS data
 * @param   buf_len     Input: Buffer size; Output: Actual SPS/PPS length
 * @return  ImgJoint_Error_t  Status code
 *
 * @pre     Encoder initialized successfully
 * @thread_safety No
 */
int h264_encoder_get_sps_pps(h264_encoder_t encoder, uint8_t* sps_pps_buf, int* buf_len);

/**
 * @brief   Destroy H264 encoder and release resources
 * @param   encoder  Encoder handle
 * @return  void
 *
 * @pre     Encoder created successfully
 * @post    All resources released, handle invalid
 * @note    Must be called to avoid memory leaks
 * @thread_safety No
 */
void h264_encoder_destroy(h264_encoder_t encoder);

/**
 * @brief   Usage Demo: YUYV to H264 RTSP Streaming
 * @code
 * #include "img_joint.h"
 * #include "rtsp_server.h"
 * #define CAM_W 640
 * #define CAM_H 360
 *
 * int main(void) {
 *     // 1. Config encoder parameters
 *     h264_encode_param_t param = {
 *         .width = CAM_W, .height = CAM_H, .fps = 15,
 *         .bitrate = 500, .gop = 15, .use_cpu_core = true
 *     };
 *
 *     // 2. Create encoder
 *     h264_encoder_t encoder = h264_encoder_create(&param);
 *     if (!encoder) return -1;
 *
 *     // 3. Start RTSP server
 *     rtsp_server_start();
 *
 *     // 4. Streaming loop
 *     uint8_t yuyv_buf[CAM_W * CAM_H * 2];
 *     uint8_t h264_buf[CAM_W * CAM_H];
 *     int h264_len;
 *
 *     while (1) {
 *         get_camera_yuyv(yuyv_buf);
 *         h264_len = sizeof(h264_buf);
 *         if (yuyv_to_h264(encoder, yuyv_buf, sizeof(yuyv_buf), h264_buf, &h264_len) == IMG_JOINT_OK) {
 *             rtsp_server_push(h264_buf, h264_len);
 *         }
 *     }
 *
 *     // 5. Release resources
 *     h264_encoder_destroy(encoder);
 *     rtsp_server_stop();
 *     return 0;
 * }
 * @endcode
 */

#ifdef __cplusplus
}
#endif

#endif // __IMG_JOINT_H__