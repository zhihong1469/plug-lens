/**
 * @file    ai_model_rknn.cpp
 * @brief   RKNN Face Detection Model Implementation
 * @details Internal implementation features:
 *          1. RKNN NPU inference engine integration
 *          2. INT8 quantization support for maximum performance
 *          3. Singleton instance with global private data
 *          4. Aspect-ratio coordinate mapping for face detection
 *          5. Static/moving face classification with color coding
 *          6. Zero-copy buffer integration with frame_link
 *          7. Memory-safe resource management
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @relies  Rockchip RKNN Runtime
 *          https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB
 * @date    2026-06-20
 * @version v1.0.0
 * @license MIT License
 */

#include "ai_model_rknn.hpp"
#include "ai_model_base.h"
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include "utils.h"
#include "img_proc_factory.h"
#include "vision_ai_config.h"
#include "img_joint.h"

/* RKNN Runtime API */
#include "rknn_api.h"

using namespace std;

// ==========================
// Private Type Definitions
// ==========================
/**
 * @brief   Private data structure for RKNN model instance
 * @details Opaque internal context for RKNN backend
 */
typedef struct {
    rknn_context             rknn_ctx;       /**< RKNN context handle */
    rknn_input_output_num    io_num;         /**< Input/output tensor count */
    rknn_tensor_attr        *input_attrs;    /**< Input tensor attributes */
    rknn_tensor_attr        *output_attrs;   /**< Output tensor attributes */
    int                      ai_w;           /**< Model input width */
    int                      ai_h;           /**< Model input height */
    const uint8_t*           frame_data;     /**< Input camera frame data */
    uint8_t*                 external_bgr_buf;/**< External BGR preprocessing buffer */
    uint8_t*                 input_buf;      /**< Model input buffer */
    uint8_t**                output_bufs;    /**< Model output buffers */
    img_proc_handle_t*       img_proc;       /**< Hardware/software backend handle */
} rknn_priv_t;

// ==========================
// Global Singleton Instance
// ==========================
/** Global model handle (singleton pattern) */
static ai_model_handle_t* g_rknn_handle = nullptr;
/** Global private data pointer */
static rknn_priv_t*       g_priv        = nullptr;

// ==========================
// Private Helper Functions
// ==========================
/**
 * @brief   Initialize RKNN model backend
 * @param   handle  Public model handle
 * @return  Error code: AI_MODEL_OK on success
 * @details Allocates memory and initializes RKNN engine
 */
static ai_model_err_t rknn_ai_init(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    rknn_priv_t* priv = new(nothrow) rknn_priv_t;
    if (!priv) return AI_MODEL_ERR_NO_MEM;
    memset(priv, 0, sizeof(rknn_priv_t));

    int ret;
    FILE* fp = fopen(handle->config.model_path, "rb");
    if (!fp) {
        delete priv;
        return AI_MODEL_ERR_LOAD;
    }

    fseek(fp, 0, SEEK_END);
    size_t model_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t* model_data = (uint8_t*)malloc(model_size);
    if (!model_data) {
        fclose(fp);
        delete priv;
        return AI_MODEL_ERR_NO_MEM;
    }
    fread(model_data, 1, model_size, fp);
    fclose(fp);

    ret = rknn_init(&priv->rknn_ctx, model_data, model_size, 0, nullptr);
    free(model_data);
    if (ret != RKNN_SUCC) {
        delete priv;
        return AI_MODEL_ERR_INIT;
    }

    ret = rknn_query(priv->rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &priv->io_num, sizeof(priv->io_num));
    if (ret != RKNN_SUCC) {
        rknn_destroy(priv->rknn_ctx);
        delete priv;
        return AI_MODEL_ERR_INIT;
    }

    priv->input_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * priv->io_num.n_input);
    priv->output_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * priv->io_num.n_output);
    if (!priv->input_attrs || !priv->output_attrs) {
        rknn_destroy(priv->rknn_ctx);
        free(priv->input_attrs);
        free(priv->output_attrs);
        delete priv;
        return AI_MODEL_ERR_NO_MEM;
    }

    for (int i = 0; i < priv->io_num.n_input; i++) {
        priv->input_attrs[i].index = i;
        ret = rknn_query(priv->rknn_ctx, RKNN_QUERY_INPUT_ATTR, &priv->input_attrs[i], sizeof(rknn_tensor_attr));
    }

    for (int i = 0; i < priv->io_num.n_output; i++) {
        priv->output_attrs[i].index = i;
        ret = rknn_query(priv->rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &priv->output_attrs[i], sizeof(rknn_tensor_attr));
    }

    priv->ai_w = handle->config.input_width;
    priv->ai_h = handle->config.input_height;

    int input_size = priv->ai_w * priv->ai_h * 3;
    priv->input_buf = (uint8_t*)malloc(input_size);
    if (!priv->input_buf) {
        rknn_destroy(priv->rknn_ctx);
        free(priv->input_attrs);
        free(priv->output_attrs);
        delete priv;
        return AI_MODEL_ERR_NO_MEM;
    }

    priv->output_bufs = (uint8_t**)malloc(sizeof(uint8_t*) * priv->io_num.n_output);
    if (!priv->output_bufs) {
        rknn_destroy(priv->rknn_ctx);
        free(priv->input_attrs);
        free(priv->output_attrs);
        free(priv->input_buf);
        delete priv;
        return AI_MODEL_ERR_NO_MEM;
    }

    for (int i = 0; i < priv->io_num.n_output; i++) {
        int output_size = 1;
        for (int j = 0; j < priv->output_attrs[i].n_dims; j++) {
            output_size *= priv->output_attrs[i].dims[j];
        }
        priv->output_bufs[i] = (uint8_t*)malloc(output_size * sizeof(float));
    }

    img_proc_config_t img_config;
    img_config.width = (int)handle->config.input_width;
    img_config.height = (int)handle->config.input_height;
    img_config.fps = GLOBAL_VIDEO_FPS;
    img_config.jpeg_quality = GLOBAL_JPEG_QUALITY;
    priv->img_proc = img_proc_factory_get_singleton(&img_config);
    if (!priv->img_proc) {
        rknn_destroy(priv->rknn_ctx);
        free(priv->input_attrs);
        free(priv->output_attrs);
        free(priv->input_buf);
        for (int i = 0; i < priv->io_num.n_output; i++) {
            free(priv->output_bufs[i]);
        }
        free(priv->output_bufs);
        delete priv;
        return AI_MODEL_ERR_INIT;
    }

    handle->user_data = priv;
    g_priv = priv;
    g_rknn_handle = handle;

    return AI_MODEL_OK;
}

/**
 * @brief   Set input frame data for model
 * @param   handle  Model handle
 * @param   data    Input frame buffer
 * @param   len     Buffer length
 * @return  Error code
 */
static ai_model_err_t rknn_ai_input(ai_model_handle_t* handle, uint8_t* data, uint32_t len)
{
    if (!handle || !data) return AI_MODEL_ERR_PARAM;

    rknn_priv_t* priv = (rknn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_ERR_INIT;

    priv->frame_data = data;
    return AI_MODEL_OK;
}

/**
 * @brief   Run RKNN inference (default MJPEG format)
 * @param   handle  Model handle
 * @return  Error code
 */
static ai_model_err_t rknn_ai_infer(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    rknn_priv_t* priv = (rknn_priv_t*)handle->user_data;
    if (!priv || !priv->frame_data || !priv->external_bgr_buf) {
        return AI_MODEL_ERR_INIT;
    }

    int ret;
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = priv->ai_w * priv->ai_h * 3;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].buf = priv->input_buf;

    rknn_output outputs[2];
    memset(outputs, 0, sizeof(outputs));
    outputs[0].want_float = true;
    outputs[1].want_float = true;

    ret = rknn_inputs_set(priv->rknn_ctx, 1, inputs);
    if (ret != RKNN_SUCC) {
        return AI_MODEL_ERR_INFER;
    }

    ret = rknn_run(priv->rknn_ctx, nullptr);
    if (ret != RKNN_SUCC) {
        return AI_MODEL_ERR_INFER;
    }

    ret = rknn_outputs_get(priv->rknn_ctx, 2, outputs, nullptr);
    if (ret != RKNN_SUCC) {
        return AI_MODEL_ERR_INFER;
    }

    memcpy(priv->output_bufs[0], outputs[0].buf, outputs[0].size);
    memcpy(priv->output_bufs[1], outputs[1].buf, outputs[1].size);

    rknn_outputs_release(priv->rknn_ctx, 2, outputs);

    return AI_MODEL_OK;
}

/**
 * @brief   Get formatted detection results
 * @param   handle        Model handle
 * @param   results       Output result array
 * @param   result_count  Output result count
 * @return  Error code
 */
static ai_model_err_t rknn_ai_get_result(ai_model_handle_t* handle,
                                         ai_model_detect_result_t* results,
                                         uint32_t* result_count)
{
    if (!handle || !results || !result_count) return AI_MODEL_ERR_PARAM;

    rknn_priv_t* priv = (rknn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_ERR_INIT;

    float* boxes = (float*)priv->output_bufs[0];
    float* scores = (float*)priv->output_bufs[1];

    const float score_thresh = handle->config.score_thresh;
    const float iou_thresh = handle->config.iou_thresh;

    vector<ai_model_detect_result_t> face_list;

    int num_anchors = 4420;
    for (int i = 0; i < num_anchors; i++) {
        float score = scores[i * 2 + 1];
        if (score >= score_thresh) {
            ai_model_detect_result_t face;
            face.x1 = boxes[i * 4];
            face.y1 = boxes[i * 4 + 1];
            face.x2 = boxes[i * 4 + 2];
            face.y2 = boxes[i * 4 + 3];
            face.score = score;
            face.class_id = 0;
            face_list.push_back(face);
        }
    }

    sort(face_list.begin(), face_list.end(), [](const ai_model_detect_result_t& a, const ai_model_detect_result_t& b) {
        return a.score > b.score;
    });

    vector<bool> keep(face_list.size(), true);
    for (int i = 0; i < face_list.size(); i++) {
        if (!keep[i]) continue;
        for (int j = i + 1; j < face_list.size(); j++) {
            if (!keep[j]) continue;
            float area_i = (face_list[i].x2 - face_list[i].x1) * (face_list[i].y2 - face_list[i].y1);
            float area_j = (face_list[j].x2 - face_list[j].x1) * (face_list[j].y2 - face_list[j].y1);
            float inter_x1 = max(face_list[i].x1, face_list[j].x1);
            float inter_y1 = max(face_list[i].y1, face_list[j].y1);
            float inter_x2 = min(face_list[i].x2, face_list[j].x2);
            float inter_y2 = min(face_list[i].y2, face_list[j].y2);
            float inter_w = max(0.f, inter_x2 - inter_x1);
            float inter_h = max(0.f, inter_y2 - inter_y1);
            float inter_area = inter_w * inter_h;
            float iou = inter_area / (area_i + area_j - inter_area);
            if (iou >= iou_thresh) {
                keep[j] = false;
            }
        }
    }

    uint32_t count = 0;
    for (int i = 0; i < face_list.size() && count < 10; i++) {
        if (keep[i]) {
            results[count++] = face_list[i];
        }
    }

    *result_count = count;
    return AI_MODEL_OK;
}

/**
 * @brief   Deinitialize and release all resources
 * @param   handle  Model handle
 * @return  Error code
 */
static ai_model_err_t rknn_ai_deinit(ai_model_handle_t* handle)
{
    if (!handle) return AI_MODEL_ERR_PARAM;

    rknn_priv_t* priv = (rknn_priv_t*)handle->user_data;
    if (!priv) return AI_MODEL_OK;

    if (priv->rknn_ctx) {
        rknn_destroy(priv->rknn_ctx);
    }

    free(priv->input_attrs);
    free(priv->output_attrs);
    free(priv->input_buf);
    for (int i = 0; i < priv->io_num.n_output; i++) {
        free(priv->output_bufs[i]);
    }
    free(priv->output_bufs);

    delete priv;
    handle->user_data = nullptr;
    g_priv = nullptr;
    g_rknn_handle = nullptr;

    return AI_MODEL_OK;
}

/**
 * @brief   Extended interface: inference with image format conversion
 * @details Adapter function wrapping ai_model_rknn_infer_image() for ops table
 */
static ai_model_err_t rknn_ai_infer_image(ai_model_handle_t* handle,
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

    int ret = ai_model_rknn_infer_image(
        image_data, cam_w, cam_h, rgb_buf,
        (FaceInfo_RKNN_C*)results, max_faces, out_face_num,
        (uint8_t)format
    );

    return (ret == RKNN_FACE_OK) ? AI_MODEL_OK : AI_MODEL_ERR_INFER;
}

/**
 * @brief   Extended interface: map coordinates and draw face boxes
 * @details Adapter function wrapping ai_model_rknn_map_and_draw_faces() for ops table
 */
static int rknn_ai_map_and_draw_faces(ai_model_handle_t* handle,
                                       ai_model_detect_result_t* faces,
                                       int face_num,
                                       int cam_w, int cam_h,
                                       const uint8_t* src_frame,
                                       uint8_t* dst_frame)
{
    (void)handle;

    if (!faces || face_num <= 0 || !src_frame || !dst_frame) {
        return 0;
    }

    return ai_model_rknn_map_and_draw_faces(
        (FaceInfo_RKNN_C*)faces, face_num, cam_w, cam_h,
        src_frame, dst_frame
    );
}

/**
 * @brief   Model operation virtual function table (extern visible for factory)
 */
extern "C" const ai_model_ops_t ai_model_rknn_ops = {
    .init       = rknn_ai_init,
    .input      = rknn_ai_input,
    .infer      = rknn_ai_infer,
    .get_result = rknn_ai_get_result,
    .deinit     = rknn_ai_deinit,
    .infer_image = rknn_ai_infer_image,
    .map_and_draw_faces = rknn_ai_map_and_draw_faces,
};

// ==========================
// Public API Implementations
// ==========================
ai_model_handle_t* ai_model_rknn_create(const ai_model_config_t* config)
{
    return ai_model_create(config, &ai_model_rknn_ops);
}

void ai_model_rknn_map_face(FaceInfo_RKNN_C* face, int cam_w, int cam_h)
{
    if (!face || !g_priv) return;

    float sw = (float)cam_w / g_priv->ai_w;
    float sh = (float)cam_h / g_priv->ai_h;
    face->x1 *= sw;
    face->y1 *= sh;
    face->x2 *= sw;
    face->y2 *= sh;
}

int ai_model_rknn_map_and_draw_faces(FaceInfo_RKNN_C* faces, int face_num,
                                     int cam_w, int cam_h,
                                     const uint8_t *src_frame, uint8_t *dst_frame)
{
    if (!faces || face_num <= 0 || !src_frame || !dst_frame || !g_priv) {
        return 0;
    }

    static FaceInfo_RKNN_C last_valid_faces[FACE_DETECT_MAX_FACES] = {0};
    static int last_face_count = 0;
    static bool is_first_frame = true;

    FaceInfo_RKNN_C curr_valid_faces[FACE_DETECT_MAX_FACES] = {0};
    bool face_moved[FACE_DETECT_MAX_FACES] = {false};
    int curr_face_count = 0;
    int need_save = 0;

    memcpy(dst_frame, src_frame, cam_w * cam_h * 3);

    int model_w = g_priv->ai_w;
    int model_h = g_priv->ai_h;

    float scale_w = (float)model_w / cam_w;
    float scale_h = (float)model_h / cam_h;
    float scale    = utils_fmaxf(scale_w, scale_h);

    float scaled_w = cam_w * scale;
    float scaled_h = cam_h * scale;
    float crop_x   = (scaled_w - model_w) / 2.0f;
    float crop_y   = (scaled_h - model_h) / 2.0f;

    const float score_thresh = 0.5f;

    for (int i = 0; i < face_num && curr_face_count < FACE_DETECT_MAX_FACES; i++) {
        FaceInfo_RKNN_C* face = &faces[i];
        if (face->score < score_thresh) continue;

        float x1 = (face->x1 + crop_x) / scale;
        float y1 = (face->y1 + crop_y) / scale;
        float x2 = (face->x2 + crop_x) / scale;
        float y2 = (face->y2 + crop_y) / scale;

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

    if (is_first_frame) {
        is_first_frame = false;
        need_save = 1;
        for (int i = 0; i < curr_face_count; i++) {
            face_moved[i] = true;
        }
    } else {
        if (curr_face_count != last_face_count) {
            need_save = 1;
            for (int i = 0; i < curr_face_count; i++) {
                face_moved[i] = true;
            }
        } else if (curr_face_count > 0) {
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

    last_face_count = curr_face_count;
    for (int i = 0; i < curr_face_count && i < FACE_DETECT_MAX_FACES; i++) {
        last_valid_faces[i] = curr_valid_faces[i];
    }

    if (need_save) {
        for (int i = 0; i < curr_face_count; i++) {
            int ix1 = (int)curr_valid_faces[i].x1;
            int iy1 = (int)curr_valid_faces[i].y1;
            int iw  = (int)(curr_valid_faces[i].x2 - curr_valid_faces[i].x1);
            int ih  = (int)(curr_valid_faces[i].y2 - curr_valid_faces[i].y1);

            if (iw < 5 || ih < 5) continue;

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

void ai_model_rknn_get_ai_size(int* w, int* h)
{
    if (w) *w = g_priv ? g_priv->ai_w : 0;
    if (h) *h = g_priv ? g_priv->ai_h : 0;
}

bool ai_model_rknn_is_ready(void)
{
    return g_priv && g_priv->rknn_ctx != 0;
}

int ai_model_rknn_infer_image(const uint8_t* image_data, int cam_w, int cam_h,
                              uint8_t* external_bgr_buf,
                              FaceInfo_RKNN_C* out_faces, int max_faces, int* out_face_num,
                              uint8_t format)
{
    if (!g_priv || !out_faces || !out_face_num || !external_bgr_buf || !g_priv->img_proc) {
        return RKNN_FACE_ERR_INPUT;
    }

    g_priv->external_bgr_buf = external_bgr_buf;

    img_proc_err_t proc_ret;
    if (format == IMG_FORMAT_YUYV) {
        proc_ret = g_priv->img_proc->ops->yuyv_to_rgb(g_priv->img_proc, image_data, external_bgr_buf);
    } else if (format == IMG_FORMAT_MJPEG) {
        proc_ret = g_priv->img_proc->ops->mjpeg_to_rgb(g_priv->img_proc,
                                                        image_data, cam_w * cam_h * 2,
                                                        external_bgr_buf);
        if (proc_ret == IMG_PROC_ERR_UNSUPPORTED) {
            extern img_proc_err_t software_mjpeg_to_rgb(img_proc_handle_t *handle,
                                                        const uint8_t *mjpeg, int mjpeg_len,
                                                        uint8_t *rgb);
            proc_ret = software_mjpeg_to_rgb(g_priv->img_proc, image_data, cam_w * cam_h * 2, external_bgr_buf);
        }
    } else {
        return RKNN_FACE_ERR_INPUT;
    }

    if (proc_ret != IMG_PROC_OK) {
        return RKNN_FACE_ERR_INFER;
    }

    int ret;
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = g_priv->ai_w * g_priv->ai_h * 3;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].buf = external_bgr_buf;

    rknn_output outputs[2];
    memset(outputs, 0, sizeof(outputs));
    outputs[0].want_float = true;
    outputs[1].want_float = true;

    ret = rknn_inputs_set(g_priv->rknn_ctx, 1, inputs);
    if (ret != RKNN_SUCC) {
        return RKNN_FACE_ERR_INFER;
    }

    ret = rknn_run(g_priv->rknn_ctx, nullptr);
    if (ret != RKNN_SUCC) {
        return RKNN_FACE_ERR_INFER;
    }

    ret = rknn_outputs_get(g_priv->rknn_ctx, 2, outputs, nullptr);
    if (ret != RKNN_SUCC) {
        return RKNN_FACE_ERR_INFER;
    }

    float* boxes = (float*)outputs[0].buf;
    float* scores = (float*)outputs[1].buf;

    const float score_thresh = 0.5f;
    const float iou_thresh = 0.45f;

    vector<FaceInfo_RKNN_C> face_list;

    int num_anchors = 4420;
    for (int i = 0; i < num_anchors; i++) {
        float score = scores[i * 2 + 1];
        if (score >= score_thresh) {
            FaceInfo_RKNN_C face;
            face.x1 = boxes[i * 4];
            face.y1 = boxes[i * 4 + 1];
            face.x2 = boxes[i * 4 + 2];
            face.y2 = boxes[i * 4 + 3];
            face.score = score;
            face_list.push_back(face);
        }
    }

    sort(face_list.begin(), face_list.end(), [](const FaceInfo_RKNN_C& a, const FaceInfo_RKNN_C& b) {
        return a.score > b.score;
    });

    vector<bool> keep(face_list.size(), true);
    for (int i = 0; i < face_list.size(); i++) {
        if (!keep[i]) continue;
        for (int j = i + 1; j < face_list.size(); j++) {
            if (!keep[j]) continue;
            float area_i = (face_list[i].x2 - face_list[i].x1) * (face_list[i].y2 - face_list[i].y1);
            float area_j = (face_list[j].x2 - face_list[j].x1) * (face_list[j].y2 - face_list[j].y1);
            float inter_x1 = max(face_list[i].x1, face_list[j].x1);
            float inter_y1 = max(face_list[i].y1, face_list[j].y1);
            float inter_x2 = min(face_list[i].x2, face_list[j].x2);
            float inter_y2 = min(face_list[i].y2, face_list[j].y2);
            float inter_w = max(0.f, inter_x2 - inter_x1);
            float inter_h = max(0.f, inter_y2 - inter_y1);
            float inter_area = inter_w * inter_h;
            float iou = inter_area / (area_i + area_j - inter_area);
            if (iou >= iou_thresh) {
                keep[j] = false;
            }
        }
    }

    *out_face_num = min((int)face_list.size(), max_faces);
    int idx = 0;
    for (int i = 0; i < face_list.size() && idx < *out_face_num; i++) {
        if (keep[i]) {
            out_faces[idx++] = face_list[i];
        }
    }

    rknn_outputs_release(g_priv->rknn_ctx, 2, outputs);

    return RKNN_FACE_OK;
}
