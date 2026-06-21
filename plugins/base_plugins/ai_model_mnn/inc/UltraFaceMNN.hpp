/**
 * @file    UltraFaceMNN.hpp
 * @brief   Ultra-Light Face Detector based on MNN Inference Engine
 * @details Core features:
 *          1. No OpenCV dependency, uses libyuv for image processing
 *          2. Optimized for NXP i.MX6ULL embedded Linux platform
 *          3. Supports dual input formats: YUYV (raw camera) and MJPEG
 *          4. Lightweight anchor-based detection with NMS suppression
 *          5. Static memory management for embedded stability
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @relies  https://github.com/alibaba/MNN
 *          https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB
 *
 * @note    Global rules:
 *          1. Single instance recommended for embedded systems.
 *          2. Call init() before detect(), deinit() on exit.
 *          3. All functions are NOT thread-safe.
 */

#ifndef ULTRA_FACE_MNN_HPP
#define ULTRA_FACE_MNN_HPP

#include <memory>
#include <vector>
#include <stdint.h>
#include "Interpreter.hpp"
#include "Tensor.hpp"
#include "ImageProcess.hpp"
/* Image processing backend: use factory pattern for hardware/software switching */
#include "img_proc_factory.h"

/**
 * @defgroup ultra_face_mnn UltraFace MNN Face Detection Module
 * @brief Lightweight face detection implementation using MNN,
 *        optimized for embedded Linux devices without OpenCV.
 * @{
 */

// ==============================================================================
// Universal Error Code Definitions
// ==============================================================================
/** Operation completed successfully */
#define MNN_FACE_OK             0
/** Module initialization failure */
#define MNN_FACE_ERR_INIT       -1
/** Invalid or corrupted MNN model file */
#define MNN_FACE_ERR_MODEL      -2
/** Invalid input parameters or null pointer */
#define MNN_FACE_ERR_INPUT      -3
/** MNN inference runtime error */
#define MNN_FACE_ERR_INFER      -4
/** MJPEG/JPEG decoding failure */
#define MNN_FACE_ERR_JPEG       -5

// ==============================================================================
// Input Image Format Enumeration
// ==============================================================================
/**
 * @brief   Supported input image formats for face detection
 * @details Matches camera output formats for embedded systems
 */
typedef enum {
    IMAGE_FORMAT_YUYV   = 0,    /**< Raw camera format: YUYV 4:2:2 */
    IMAGE_FORMAT_MJPEG  = 1     /**< Compressed format: MJPEG */
} ImageFormat;

// ==============================================================================
// Embedded Platform Default Configuration
// ==============================================================================
/** Default model input width (optimized for i.MX6ULL) */
#define DEFAULT_AI_W            320
/** Default model input height (optimized for i.MX6ULL) */
#define DEFAULT_AI_H            240
/** Default inference thread count (single thread for embedded CPU) */
#define DEFAULT_NUM_THREAD      1
/** Default face confidence threshold */
#define DEFAULT_SCORE_THRESH    0.65f
/** Default NMS (Non-Maximum Suppression) threshold */
#define DEFAULT_IOU_THRESH      0.3f

// ==============================================================================
// Face Detection Result Structure
// ==============================================================================
/**
 * @brief   Face detection output structure (raw model coordinates)
 * @details Stores bounding box and confidence score for detected faces
 * @note    Coordinates are relative to model input resolution
 */
typedef struct {
    float x1;      /**< Top-left x coordinate of face bounding box */
    float y1;      /**< Top-left y coordinate of face bounding box */
    float x2;      /**< Bottom-right x coordinate of face bounding box */
    float y2;      /**< Bottom-right y coordinate of face bounding box */
    float score;   /**< Detection confidence score (0.0 ~ 1.0) */
} FaceInfo_MNN;

// ==============================================================================
// UltraFace MNN Core Class
// ==============================================================================
/**
 * @brief   MNN-based UltraFace lightweight face detector
 * @details Embedded-optimized face detection class:
 *          - libyuv for image conversion & scaling
 *          - No third-party GUI dependencies
 *          - Fixed-point inference for low-power CPUs
 */
class UltraFaceMNN {
public:
    /**
     * @brief   Class constructor
     * @details Initialize default parameters and anchor box settings
     * @thread_safety No
     */
    UltraFaceMNN();

    /**
     * @brief   Class destructor
     * @details Auto-release MNN resources and memory
     * @thread_safety No
     */
    ~UltraFaceMNN();

    /**
     * @brief   Initialize MNN model and detection parameters
     * @param   model_path      File path of MNN model file
     * @param   ai_w            Model input width
     * @param   ai_h            Model input height
     * @param   score_threshold Confidence threshold for valid faces
     * @param   iou_threshold   IOU threshold for NMS suppression
     * @return  MNN_FACE_OK on success, negative error code on failure
     * @pre     Model file must exist and be readable
     * @post    Module enters ready state if initialization succeeds
     * @warning Do not call this function multiple times
     * @thread_safety No
     */
    int init(const char* model_path, 
            int ai_w, 
            int ai_h,
            float score_threshold = DEFAULT_SCORE_THRESH,
            float iou_threshold = DEFAULT_IOU_THRESH);

    /**
     * @brief   Run face detection inference
     * @param   input_data      Pointer to raw input image data
     * @param   cam_w           Original camera/image width
     * @param   cam_h           Original camera/image height
     * @param   external_rgb_buf External RGB buffer for format conversion
     * @param   face_list       Output list of detected faces
     * @param   format          Input image format (YUYV/MJPEG)
     * @return  MNN_FACE_OK on success, negative error code on failure
     * @pre     Module must be initialized (is_ready() == true)
     * @pre     All input buffers must be valid and non-null
     * @post    face_list contains valid detection results
     * @note    Uses libyuv for hardware-accelerated image processing
     * @thread_safety No
     */
    int detect(const uint8_t* input_data, 
               int cam_w, 
               int cam_h,
               uint8_t* external_rgb_buf,
               std::vector<FaceInfo_MNN>& face_list,
               ImageFormat format = IMAGE_FORMAT_YUYV);

    /**
     * @brief   Run face detection with pre-converted RGB input
     * @param   rgb_data        Pointer to RGB888 image data (already converted)
     * @param   cam_w           Image width
     * @param   cam_h           Image height
     * @param   face_list       Output list of detected faces
     * @return  MNN_FACE_OK on success, negative error code on failure
     * @pre     Module must be initialized (is_ready() == true)
     * @pre     rgb_data must point to valid RGB888 buffer
     * @post    face_list contains valid detection results
     * @note    Used with img_proc_factory backend for hardware acceleration
     * @thread_safety No
     */
    int detect_rgb_only(const uint8_t* rgb_data,
                       int cam_w,
                       int cam_h,
                       std::vector<FaceInfo_MNN>& face_list);

    /**
     * @brief   Map model coordinates to original image resolution
     * @param   face    Face result to be mapped
     * @param   ai_w    Model input width
     * @param   ai_h    Model input height
     * @param   cam_w   Original image width
     * @param   cam_h   Original image height
     * @return  None
     * @note    Static utility function, no instance dependency
     * @thread_safety Yes
     */
    static void map_face_to_original(FaceInfo_MNN& face, int ai_w, int ai_h, int cam_w, int cam_h);

    /**
     * @brief   Release all MNN and memory resources
     * @return  None
     * @pre     Module must be initialized
     * @post    Module enters non-ready state, all resources freed
     * @thread_safety No
     */
    void deinit();
    
    /**
     * @brief   Check module initialization status
     * @return  True = ready for inference, False = not initialized
     * @thread_safety Yes
     */
    bool is_ready() const { return m_ready; }

private:
    /**
     * @brief   Generate candidate bounding boxes from model output
     * @param   bbox_collection  Output list of raw candidate boxes
     * @param   scores            MNN tensor for confidence scores
     * @param   boxes             MNN tensor for bounding box coordinates
     * @return  None
     * @details Decode model output using anchor boxes and variance parameters
     */
    void generate_bbox(std::vector<FaceInfo_MNN>& bbox_collection, MNN::Tensor* scores, MNN::Tensor* boxes);

    /**
     * @brief   Non-Maximum Suppression (NMS) for redundant box removal
     * @param   input   Raw candidate face boxes
     * @param   output  Final filtered face results
     * @return  None
     * @details Sort boxes by score and suppress overlapping boxes
     */
    void nms(std::vector<FaceInfo_MNN>& input, std::vector<FaceInfo_MNN>& output);

private:
    // MNN Core Resources
    std::shared_ptr<MNN::Interpreter> m_interpreter;  /**< MNN model interpreter */
    MNN::Session* m_session;                          /**< MNN inference session */
    MNN::Tensor* m_input_tensor;                      /**< MNN input image tensor */

    // Image Processing Backend (factory pattern for RGA/software switch)
    img_proc_handle_t* m_img_proc;                    /**< Image processing handle (singleton) */

    // Model Configuration
    int m_ai_w;                /**< Model input width */
    int m_ai_h;                /**< Model input height */
    int m_num_thread;          /**< Inference thread count */
    float m_score_thresh;      /**< Face confidence threshold */
    float m_iou_thresh;        /**< NMS IOU threshold */
    bool m_ready;              /**< Module ready flag */

    // Image Preprocessing Parameters
    const float m_mean_vals[3];    /**< RGB mean normalization values */
    const float m_norm_vals[3];    /**< RGB scale normalization values */
    
    // Anchor Box Parameters (UltraFace Algorithm)
    const float m_center_variance; /**< Center coordinate variance */
    const float m_size_variance;   /**< Box size variance */
    std::vector<std::vector<float>> m_min_boxes;  /**< Anchor box sizes */
    std::vector<float> m_strides;                /**< Feature map strides */
    std::vector<std::vector<float>> m_priors;     /**< Precomputed anchor priors */
    int m_num_anchors;                            /**< Total anchor count */
};

/** @} */

#endif // ULTRA_FACE_MNN_HPP