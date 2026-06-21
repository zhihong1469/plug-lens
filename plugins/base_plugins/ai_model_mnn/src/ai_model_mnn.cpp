/**
 * @file    ai_model_mnn.cpp
 * @brief   MNN Face Detection Model Implementation
 * @details Internal implementation features:
 *          1. C++ wrapper for MNN inference engine
 *          2. Singleton instance with global private data
 *          3. Aspect-ratio coordinate mapping for face detection
 *          4. Static/moving face classification with color coding
 *          5. Zero-copy buffer integration with frame_link
 *          6. Memory-safe resource management
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @relies  https://github.com/alibaba/MNN
 *          https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */

#include "ai_model_mnn.hpp"
#include "ai_model_base.h"
#include "UltraFaceMNN.hpp"
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#include "utils.h"
/* Image processing backend: use factory pattern for hardware/software switching */
#include "img_proc_factory.h"
/* Drawing utilities (bgr_draw_rect) */
#include "img_joint.h"

using namespace std;

// ==========================
// Private Type Definitions
// ==========================
/**
 * @brief   Private data structure for MNN model instance
 * @details Opaque internal context for C++ backend
 */
typedef struct {
    UltraFaceMNN*            ultra_face;     /**< MNN UltraFace model instance */
    int                      ai_w;           /**< Model input width */
    int                      ai_h;           /**< Model input height */
    const uint8_t*           frame_data;     /**< Input camera frame data */
    uint8_t*                 external_bgr_buf;/**< External BGR preprocessing buffer */
    vector<FaceInfo_MNN>     curr_faces;     /**< Internal face detection results */
    /* Image processing backend handle (factory pattern) */
    img_proc_handle_t*       img_proc;       /**< Hardware/software backend handle */
} mnn_priv_t;

// ==========================
// Global Singleton Instance
// ==========================
/** Global model handle (singleton pattern) */
static ai_model_handle_t* g_mnn_handle = nullptr;
/** Global private data pointer */
static mnn_priv_t*        g_priv        = nullptr;

// ==========================
// Private Helper Functions
// ==========================
/**
 * @brief   Initialize MNN model backend
 * @param   handle  Public model handle
 * @return  Error code: AI_MODEL_OK on success
 * @details Allocates memory and initializes MNN engine
 */
static ai_model_err_t mnn_ai_init(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = new(nothrow) mnn_priv_t;
    if (!priv) return AI_MODEL_ERR_NO_MEM;
    memset(priv, 0, sizeof(mnn_priv_t));

    priv->ultra_face = new(nothrow) UltraFaceMNN();
    if (!priv->ultra_face) {
        delete priv;
        return AI_MODEL_ERR_NO_MEM;
    }

    int ret = priv->ultra_face->init(
        handle->config.model_path,
        handle->config.input_width,
        handle->config.input_height,
        handle->config.score_thresh,
        handle->config.iou_thresh
    );

    if (ret != MNN_FACE_OK) {
        delete priv->ultra_face;
        delete priv;
        return AI_MODEL_ERR_INIT;
    }

    /* Get shared image processing singleton (RGA or software) */
    img_proc_config_t img_config;
    img_config.width = (int)handle->config.input_width;
    img_config.height = (int)handle->config.input_height;
    img_config.fps = 30;
    img_config.jpeg_quality = 85;
    priv->img_proc = img_proc_factory_get_singleton(&img_config);
    if (!priv->img_proc) {
        priv->ultra_face->deinit();
        delete priv->ultra_face;
        delete priv;
        return AI_MODEL_ERR_INIT;
    }
    /* Singleton is auto-initialized by factory, no need to call init() */

    priv->ai_w = handle->config.input_width;
    priv->ai_h = handle->config.input_height;
    handle->user_data = priv;
    
    g_priv = priv;
    g_mnn_handle = handle;

    return AI_MODEL_OK;
}

/**
 * @brief   Set input frame data for model
 * @param   handle  Model handle
 * @param   data    Input frame buffer
 * @param   len     Buffer length
 * @return  Error code
 */
static ai_model_err_t mnn_ai_input(ai_model_handle_t* handle, uint8_t* data, uint32_t len)
{
    if (!handle || !data) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_ERR_INIT;

    priv->frame_data = data;
    return AI_MODEL_OK;
}

/**
 * @brief   Run MNN inference (default MJPEG format)
 * @param   handle  Model handle
 * @return  Error code
 */
static ai_model_err_t mnn_ai_infer(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv || !priv->frame_data || !priv->external_bgr_buf) {
        return AI_MODEL_ERR_INIT;
    }

    priv->curr_faces.clear();
    // Default to MJPEG for embedded hardware optimization
    int ret = priv->ultra_face->detect(
        priv->frame_data,
        priv->ai_w,
        priv->ai_h,
        priv->external_bgr_buf,
        priv->curr_faces,
        IMAGE_FORMAT_MJPEG
    );

    return (ret == MNN_FACE_OK) ? AI_MODEL_OK : AI_MODEL_ERR_INFER;
}

/**
 * @brief   Get formatted detection results
 * @param   handle        Model handle
 * @param   results       Output result array
 * @param   result_count  Output result count
 * @return  Error code
 */
static ai_model_err_t mnn_ai_get_result(ai_model_handle_t* handle,
                                       ai_model_detect_result_t* results,
                                       uint32_t* result_count)
{
    if (!handle || !results || !result_count) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_ERR_INIT;

    uint32_t num = min(priv->curr_faces.size(), (size_t)10);
    *result_count = num;

    for (uint32_t i = 0; i < num; i++) {
        auto& f = priv->curr_faces[i];
        results[i].x1 = f.x1;
        results[i].y1 = f.y1;
        results[i].x2 = f.x2;
        results[i].y2 = f.y2;
        results[i].score = f.score;
        results[i].class_id = 0;
    }

    return AI_MODEL_OK;
}

/**
 * @brief   Deinitialize and release all resources
 * @param   handle  Model handle
 * @return  Error code
 */
static ai_model_err_t mnn_ai_deinit(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    mnn_priv_t* priv = (mnn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_OK;

    /* Note: img_proc is a singleton, do NOT destroy it here.
     * The singleton persists for the lifetime of the process. */

    if (priv->ultra_face) {
        priv->ultra_face->deinit();
        delete priv->ultra_face;
    }

    delete priv;
    handle->user_data = nullptr;
    g_priv = nullptr;
    g_mnn_handle = nullptr;

    return AI_MODEL_OK;
}

/**
 * @brief   Extended interface: inference with image format conversion
 * @details Adapter function wrapping ai_model_mnn_infer_image() for ops table
 * @param   handle      Model handle
 * @param   image_data  Input camera frame data
 * @param   cam_w       Camera frame width
 * @param   cam_h       Camera frame height
 * @param   rgb_buf     External BGR buffer for preprocessing
 * @param   results     Output detection results array
 * @param   max_faces   Maximum faces to detect
 * @param   out_face_num Output: actual detected face count
 * @param   format      Input format (INPUT_FORMAT_YUYV/INPUT_FORMAT_MJPEG)
 * @return  AI_MODEL_OK on success, negative error code on failure
 */
static ai_model_err_t mnn_ai_infer_image(ai_model_handle_t* handle,
                                          uint8_t* image_data,
                                          int cam_w, int cam_h,
                                          uint8_t* rgb_buf,
                                          ai_model_detect_result_t* results,
                                          int max_faces,
                                          int* out_face_num,
                                          int format)
{
    if (!handle || !image_data || !rgb_buf || !results || !out_face_num) {
        return AI_MODEL_ERR_PARAM;
    }

    /* Call standalone MNN inference function */
    int ret = ai_model_mnn_infer_image(
        image_data, cam_w, cam_h, rgb_buf,
        (FaceInfo_C*)results, max_faces, out_face_num,
        (uint8_t)format
    );

    return (ret == MNN_FACE_OK) ? AI_MODEL_OK : AI_MODEL_ERR_INFER;
}

/**
 * @brief   Extended interface: map coordinates and draw face boxes
 * @details Adapter function wrapping ai_model_mnn_map_and_draw_faces() for ops table
 * @param   handle      Model handle (unused, uses global g_priv)
 * @param   faces       Detection results array
 * @param   face_num    Number of detected faces
 * @param   cam_w       Camera frame width
 * @param   cam_h       Camera frame height
 * @param   src_frame   Source RGB frame buffer
 * @param   dst_frame   Output RGB frame buffer with boxes drawn
 * @return  1 = need save image, 0 = no save
 */
static int mnn_ai_map_and_draw_faces(ai_model_handle_t* handle,
                                      ai_model_detect_result_t* faces,
                                      int face_num,
                                      int cam_w, int cam_h,
                                      const uint8_t* src_frame,
                                      uint8_t* dst_frame)
{
    (void)handle;  /* Unused - uses global singleton g_priv */

    if (!faces || face_num <= 0 || !src_frame || !dst_frame) {
        return 0;
    }

    /* Call standalone MNN drawing function */
    return ai_model_mnn_map_and_draw_faces(
        (FaceInfo_C*)faces, face_num, cam_w, cam_h,
        src_frame, dst_frame
    );
}

/**
 * @brief   Model operation virtual function table (extern visible for factory)
 * @note    Removed 'static' to allow factory pattern linking
 */
extern "C" const ai_model_ops_t ai_model_mnn_ops = {
    .init       = mnn_ai_init,
    .input      = mnn_ai_input,
    .infer      = mnn_ai_infer,
    .get_result = mnn_ai_get_result,
    .deinit     = mnn_ai_deinit,
    /* Extended interfaces for face detection */
    .infer_image = mnn_ai_infer_image,
    .map_and_draw_faces = mnn_ai_map_and_draw_faces,
};
// ==========================
// Public API Implementations
// ==========================
/**
 * @brief   Create model instance (base interface wrapper)
 */
ai_model_handle_t* ai_model_mnn_create(const ai_model_config_t* config)
{
    return ai_model_create(config, &ai_model_mnn_ops);
}

/**
 * @brief   Map face coordinates to camera resolution
 */
void ai_model_mnn_map_face(FaceInfo_C* face, int cam_w, int cam_h)
{
    if (!face || !g_priv) return;

    float sw = (float)cam_w / g_priv->ai_w;
    float sh = (float)cam_h / g_priv->ai_h;
    face->x1 *= sw;
    face->y1 *= sh;
    face->x2 *= sw;
    face->y2 *= sh;
}

/**
 * @brief   Map coordinates + draw boxes + motion detection
 * @details Core logic:
 *          1. Aspect-ratio coordinate correction
 *          2. Static/moving face classification
 *          3. Tri-color box drawing
 *          4. Auto image save trigger
 */
int ai_model_mnn_map_and_draw_faces(FaceInfo_C* faces, int face_num, 
                                     int cam_w, int cam_h,
                                     const uint8_t *src_frame, uint8_t *dst_frame)
{
    if (!faces || face_num <= 0 || !src_frame || !dst_frame || !g_priv) {
        return 0;
    }

    // Static cache for motion comparison between frames
    static FaceInfo_C last_valid_faces[FACE_DETECT_MAX_FACES] = {0};
    static int last_face_count = 0;
    static bool is_first_frame = true;

    FaceInfo_C curr_valid_faces[FACE_DETECT_MAX_FACES] = {0};
    bool face_moved[FACE_DETECT_MAX_FACES] = {false};
    int curr_face_count = 0;
    int need_save = 0;

    // Copy source frame to output buffer
    memcpy(dst_frame, src_frame, cam_w * cam_h * 3);

    int model_w = g_priv->ai_w;
    int model_h = g_priv->ai_h;

    // Aspect ratio scaling (consistent with image resizing logic)
    float scale_w = (float)model_w / cam_w;
    float scale_h = (float)model_h / cam_h;
    float scale    = utils_fmaxf(scale_w, scale_h);

    float scaled_w = cam_w * scale;
    float scaled_h = cam_h * scale;
    float crop_x   = (scaled_w - model_w) / 2.0f;
    float crop_y   = (scaled_h - model_h) / 2.0f;

    const float score_thresh = 0.5f;

    // Step 1: Filter valid faces
    for (int i = 0; i < face_num && curr_face_count < FACE_DETECT_MAX_FACES; i++) 
    {
        FaceInfo_C* face = &faces[i];
        if (face->score < score_thresh) continue;

        // Convert model coordinates to original image coordinates
        float x1 = (face->x1 + crop_x) / scale;
        float y1 = (face->y1 + crop_y) / scale;
        float x2 = (face->x2 + crop_x) / scale;
        float y2 = (face->y2 + crop_y) / scale;

        // Boundary protection
        x1 = utils_fmaxf(0.0f, utils_fminf(x1, (float)cam_w));
        y1 = utils_fmaxf(0.0f, utils_fminf(y1, (float)cam_h));
        x2 = utils_fmaxf(0.0f, utils_fminf(x2, (float)cam_w));
        y2 = utils_fmaxf(0.0f, utils_fminf(y2, (float)cam_h));

        if (x2 <= x1 || y2 <= y1) continue;

        curr_valid_faces[curr_face_count] = *face;
        curr_valid_faces[curr_face_count].x1 = x1;
        curr_valid_faces[curr_face_count].y1 = y1;
        curr_valid_faces[curr_face_count].x2 = x2;
        curr_valid_faces[curr_face_count].y2 = y2;
        curr_face_count++;
    }

    // Step 2: Motion detection logic
    if (is_first_frame) {
        is_first_frame = false;
        need_save = 1;
        for (int i = 0; i < curr_face_count; i++) {
            face_moved[i] = true;
        }
    } else {
        // Trigger save if face count changes
        if (curr_face_count != last_face_count) {
            need_save = 1;
            for (int i = 0; i < curr_face_count; i++) {
                face_moved[i] = true;
            }
        }
        // Center distance comparison for same face count
        else if (curr_face_count > 0) {
            for (int i = 0; i < curr_face_count; i++) {
                face_moved[i] = true;
                float curr_cx = (curr_valid_faces[i].x1 + curr_valid_faces[i].x2) / 2.0f;
                float curr_cy = (curr_valid_faces[i].y1 + curr_valid_faces[i].y2) / 2.0f;

                for (int j = 0; j < last_face_count; j++) {
                    float last_cx = (last_valid_faces[j].x1 + last_valid_faces[j].x2) / 2.0f;
                    float last_cy = (last_valid_faces[j].y1 + last_valid_faces[j].y2) / 2.0f;

                    float dx = utils_fabsf(curr_cx - last_cx);
                    float dy = utils_fabsf(curr_cy - last_cy);
                    if (dx < FACE_STATIC_THRESHOLD && dy < FACE_STATIC_THRESHOLD) {
                        face_moved[i] = false;
                        break;
                    }
                }
                if (face_moved[i]) need_save = 1;
            }
        }
    }

    // Update cache for next frame
    last_face_count = curr_face_count;
    for (int i = 0; i < curr_face_count && i < FACE_DETECT_MAX_FACES; i++) {
        last_valid_faces[i] = curr_valid_faces[i];
    }

    // Draw boxes only when image save is required
    if (need_save) {
        for (int i = 0; i < curr_face_count; i++)
        {
            int ix1 = (int)curr_valid_faces[i].x1;
            int iy1 = (int)curr_valid_faces[i].y1;
            int iw  = (int)(curr_valid_faces[i].x2 - curr_valid_faces[i].x1);
            int ih  = (int)(curr_valid_faces[i].y2 - curr_valid_faces[i].y1);

            if (iw < 5 || ih < 5) continue;

            // Select box color by status
            uint32_t box_color;
            if (curr_valid_faces[i].score < FACE_LOW_SCORE_THRESH) {
                box_color = FACE_BOX_COLOR_GREEN;
            } else if (face_moved[i]) {
                box_color = FACE_BOX_COLOR_RED;
            } else {
                box_color = FACE_BOX_COLOR_BLUE;
            }

            bgr_draw_rect(dst_frame, cam_w, cam_h, ix1, iy1, iw, ih, box_color, FACE_BOX_THICKNESS);
        }
    }

    return need_save;
}

/**
 * @brief   Get model input resolution
 */
void ai_model_mnn_get_ai_size(int* w, int* h)
{
    if (w) *w = g_priv ? g_priv->ai_w : 0;
    if (h) *h = g_priv ? g_priv->ai_h : 0;
}

/**
 * @brief   Check model ready status
 */
bool ai_model_mnn_is_ready(void)
{
    return g_priv && g_priv->ultra_face && g_priv->ultra_face->is_ready();
}

/**
 * @brief   Universal inference with format support
 * @details Uses img_proc_factory backend (RGA or software) for format conversion
 */
int ai_model_mnn_infer_image(const uint8_t* image_data, int cam_w, int cam_h,
                            uint8_t* external_bgr_buf,
                            FaceInfo_C* out_faces, int max_faces, int* out_face_num,
                            uint8_t format)
{
    if (!g_priv || !out_faces || !out_face_num || !external_bgr_buf || !g_priv->img_proc) {
        return MNN_FACE_ERR_INPUT;
    }

    g_priv->external_bgr_buf = external_bgr_buf;

    /* Use img_proc_factory backend for format conversion (RGA or software) */
    img_proc_err_t proc_ret;
    if (format == IMG_FORMAT_YUYV) {
        proc_ret = g_priv->img_proc->ops->yuyv_to_rgb(g_priv->img_proc, image_data, external_bgr_buf);
    } else if (format == IMG_FORMAT_MJPEG) {
        /* MJPEG decode not supported by RGA, fallback to software */
        proc_ret = g_priv->img_proc->ops->mjpeg_to_rgb(g_priv->img_proc, 
                                                         image_data, cam_w * cam_h * 2,
                                                         external_bgr_buf);
        if (proc_ret == IMG_PROC_ERR_UNSUPPORTED) {
            /* Fallback: use software implementation directly if RGA doesn't support MJPEG */
            extern img_proc_err_t software_mjpeg_to_rgb(img_proc_handle_t *handle,
                                                        const uint8_t *mjpeg, int mjpeg_len,
                                                        uint8_t *rgb);
            proc_ret = software_mjpeg_to_rgb(g_priv->img_proc, image_data, cam_w * cam_h * 2, external_bgr_buf);
        }
    } else {
        return MNN_FACE_ERR_INPUT;
    }

    if (proc_ret != IMG_PROC_OK) {
        return MNN_FACE_ERR_INFER;
    }

    /* Call UltraFaceMNN for detection (input is already converted RGB) */
    vector<FaceInfo_MNN> faces;
    int ret = g_priv->ultra_face->detect_rgb_only(
        external_bgr_buf, 
        cam_w, 
        cam_h, 
        faces
    );
    if (ret != MNN_FACE_OK) {
        return ret;
    }

    *out_face_num = min((int)faces.size(), max_faces);
    for (int i = 0; i < *out_face_num; i++) {
        out_faces[i] = {faces[i].x1, faces[i].y1, faces[i].x2, faces[i].y2, faces[i].score};
    }

    return MNN_FACE_OK;
}

