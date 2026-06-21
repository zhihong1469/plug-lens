/**
 * @file    ai_model_rknn.hpp
 * @brief   RKNN UltraFace Face Detection Model - Pure C External Interface
 * @details Core capabilities for Rockchip RK3562 NPU acceleration:
 *          1. Optimized for RK3562 NPU hardware acceleration
 *          2. INT8 quantization support for maximum performance
 *          3. Zero-copy integration with frame_link subsystem
 *          4. Pure C API hides underlying implementation
 *          5. Global singleton design compliant with SOA
 *          6. OpenCV-accelerated face box drawing
 *          7. Dual input support: YUYV and MJPEG formats
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-06-20
 * @version v1.0.0
 * @license MIT License
 *
 * @relies  https://github.com/alibaba/MNN
 *          https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB
 *          Rockchip RKNN Toolkit2
 *
 * @note    Global rules:
 *          1. All functions are not thread-safe unless marked specially.
 *          2. Call functions in order: create → infer → destroy.
 *          3. Only one instance is allowed in a single process.
 */
#ifndef __AI_MODEL_RKNN_HPP
#define __AI_MODEL_RKNN_HPP

#include "ai_model_base.h"
#include <stdint.h>
#include <stdbool.h>


/**
 * @defgroup ai_model_rknn RKNN Face Detection Model (Pure C API)
 * @brief RKNN-based UltraFace face detection implementation,
 *        compliant with ai_model_base abstract interface.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

// ==============================================================================
// Error Code Definitions
// ==============================================================================
/** Operation completed successfully */
#define RKNN_FACE_OK             0
/** Model initialization failure */
#define RKNN_FACE_ERR_INIT       -1
/** Invalid or corrupted model file */
#define RKNN_FACE_ERR_MODEL      -2
/** Invalid input parameters or null pointer */
#define RKNN_FACE_ERR_INPUT      -3
/** RKNN runtime inference failure */
#define RKNN_FACE_ERR_INFER      -4

// ==============================================================================
// Input Image Format
// ==============================================================================
/** Input image format: YUYV */
#define INPUT_FORMAT_YUYV       0
/** Input image format: MJPEG */
#define INPUT_FORMAT_MJPEG      1

// ==============================================================================
// Drawing Configuration
// ==============================================================================
/** Line thickness for face bounding box */
#define FACE_BOX_THICKNESS      2
/** Box color: static face (Blue, BGR format) */
#define FACE_BOX_COLOR_BLUE     0xFF0000
/** Box color: moving face (Red, BGR format) */
#define FACE_BOX_COLOR_RED      0x0000FF
/** Box color: low-confidence face (Green, BGR format) */
#define FACE_BOX_COLOR_GREEN    0x00FF00

/** Confidence threshold for low-score face marking */
#define FACE_LOW_SCORE_THRESH   0.6f
/** Movement threshold for static face detection */
#define FACE_STATIC_THRESHOLD   10.0f
/** Maximum number of detectable faces supported */
#define FACE_DETECT_MAX_FACES   10

// ==============================================================================
// Face Information Structure (For Upper Layer)
// ==============================================================================
/**
 * @brief   Face detection result structure (raw model output)
 * @details Stores bounding box coordinates and confidence score
 * @note    Coordinates are raw model output, need mapping to camera resolution
 */
typedef struct {
    float x1;      /**< Top-left x coordinate of bounding box */
    float y1;      /**< Top-left y coordinate of bounding box */
    float x2;      /**< Bottom-right x coordinate of bounding box */
    float y2;      /**< Bottom-right y coordinate of bounding box */
    float score;   /**< Detection confidence, range: 0.0 ~ 1.0 */
} FaceInfo_RKNN_C;

// ==============================================================================
// Public Core APIs
// ==============================================================================

/**
 * @brief   Create and initialize RKNN face detection singleton instance
 * @param   config  Pointer to model configuration structure
 * @return  Valid model handle on success, NULL on failure
 * @pre     Model file path must be valid and accessible
 * @post    Model enters ready state if initialization succeeds
 * @note    Global singleton: only one instance can exist
 * @warning Do not call this function multiple times
 * @thread_safety No
 */
ai_model_handle_t* ai_model_rknn_create(const ai_model_config_t* config);

/**
 * @brief   Map raw face coordinates to original camera resolution
 * @param   face    Pointer to face result structure
 * @param   cam_w   Original camera frame width
 * @param   cam_h   Original camera frame height
 * @return  None
 * @pre     Model instance must be initialized
 * @pre     Valid face structure pointer
 * @thread_safety No
 */
void ai_model_rknn_map_face(FaceInfo_RKNN_C* face, int cam_w, int cam_h);

/**
 * @brief   Map coordinates and draw colored face boxes (all-in-one)
 * @param   faces       Array of face detection results
 * @param   face_num    Number of detected faces
 * @param   cam_w       Camera frame width
 * @param   cam_h       Camera frame height
 * @param   src_frame   Pointer to source input image buffer
 * @param   dst_frame   Pointer to output image buffer with boxes drawn
 * @return  1 = need save image, 0 = no save (static face)
 * @pre     Input buffers and model instance must be valid
 * @note    Uses OpenCV for high-performance drawing
 * @note    Color logic: Red(moving), Blue(static), Green(low score)
 * @thread_safety No
 */
int ai_model_rknn_map_and_draw_faces(FaceInfo_RKNN_C* faces, int face_num,
                                     int cam_w, int cam_h,
                                     const uint8_t *src_frame, uint8_t *dst_frame);

/**
 * @brief   Get model input resolution (width & height)
 * @param   w   Output pointer to store model width
 * @param   h   Output pointer to store model height
 * @return  None
 * @pre     Model instance must be initialized
 * @thread_safety No
 */
void ai_model_rknn_get_ai_size(int* w, int* h);

/**
 * @brief   Check if model is ready for inference
 * @return  True = ready, False = not initialized/error
 * @thread_safety No
 */
bool ai_model_rknn_is_ready(void);

/**
 * @brief   Universal face detection inference (YUYV/MJPEG compatible)
 * @param   image_data      Pointer to input camera frame data
 * @param   cam_w           Camera frame width
 * @param   cam_h           Camera frame height
 * @param   external_bgr_buf Isolated BGR buffer for preprocessing
 * @param   out_faces       Output array for face results
 * @param   max_faces       Maximum faces to store in output
 * @param   out_face_num    Output pointer for actual detected face count
 * @param   format          Input format: 0=YUYV, 1=MJPEG
 * @return  RKNN_FACE_OK on success, negative error code on failure
 * @pre     Model initialized and input buffers valid
 * @post    out_faces and out_face_num populated with results
 * @warning Input frame buffer must remain valid during inference
 * @thread_safety No
 */
int ai_model_rknn_infer_image(const uint8_t* image_data, int cam_w, int cam_h,
                             uint8_t* external_bgr_buf,
                             FaceInfo_RKNN_C* out_faces, int max_faces, int* out_face_num,
                             uint8_t format);

// ==============================================================================
// Usage Example
// ==============================================================================
/**
 * @example Usage demo
 * @code
 * // 1. Initialize model
 * ai_model_config_t cfg = {
 *     .model_path = "/mnt/face.rknn",
 *     .input_width = 320,
 *     .input_height = 320,
 *     .score_thresh = 0.5f,
 *     .iou_thresh = 0.3f,
 * };
 * ai_model_handle_t* handle = ai_model_rknn_create(&cfg);
 *
 * // 2. Run inference
 * const uint8_t* cam_frame = get_camera_frame();
 * uint8_t* bgr_buf = get_bgr_buffer();
 * FaceInfo_RKNN_C faces[10];
 * int face_num = 0;
 *
 * int ret = ai_model_rknn_infer_image(
 *     cam_frame, 640, 360,
 *     bgr_buf, faces, 10, &face_num,
 *     INPUT_FORMAT_YUYV
 * );
 *
 * // 3. Draw and save
 * ai_model_rknn_map_and_draw_faces(
 *     faces, face_num, 640, 360,
 *     cam_frame, bgr_buf
 * );
 * @endcode
 */

#ifdef __cplusplus
}
#endif

/** @} */

#endif // __AI_MODEL_RKNN_HPP
