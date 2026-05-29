/**
 * @file    UltraFaceMNN.cpp
 * @brief   UltraFace MNN Face Detection Implementation
 * @details Internal design & optimizations:
 *          1. Anchor-based detection with precomputed prior boxes
 *          2. libyuv acceleration for image conversion/resizing
 *          3. Low-precision inference for i.MX6ULL CPU optimization
 *          4. Memory-safe buffer management (no runtime leaks)
 *          5. NMS suppression for redundant face removal
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @relies  https://github.com/alibaba/MNN
 *          https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */

#include "UltraFaceMNN.hpp"
#include <string.h>
#include <algorithm>
#include <math.h>
#include "img_joint.h"

/** Clip value to range [0, y] */
#define clip(x, y) (x < 0 ? 0 : (x > y ? y : x))

// ==============================================================================
// Constructor & Destructor
// ==============================================================================
UltraFaceMNN::UltraFaceMNN() 
    : m_session(nullptr), m_input_tensor(nullptr), m_ready(false),
      m_num_thread(DEFAULT_NUM_THREAD), 
      m_score_thresh(DEFAULT_SCORE_THRESH),
      m_iou_thresh(DEFAULT_IOU_THRESH),
      m_mean_vals{127, 127, 127},
      m_norm_vals{1.0f / 128, 1.0f / 128, 1.0f / 128},
      m_center_variance(0.1f),
      m_size_variance(0.2f)
{
    // Initialize UltraFace anchor box sizes (fixed algorithm parameters)
    m_min_boxes = {{10.0f,16.0f,24.0f}, {32.0f,48.0f}, {64.0f,96.0f}, {128.0f,192.0f,256.0f}};
    // Initialize feature map strides (fixed algorithm parameters)
    m_strides = {8.0f, 16.0f, 32.0f, 64.0f};
}

UltraFaceMNN::~UltraFaceMNN() {
    // Auto-release resources on destruction
    deinit();
}

// ==============================================================================
// Public: Model Initialization
// ==============================================================================
int UltraFaceMNN::init(const char* model_path, int ai_w, int ai_h,
                       float score_threshold, float iou_threshold) {
    if (!model_path) return MNN_FACE_ERR_INPUT;

    // Save configuration parameters
    m_ai_w = ai_w;
    m_ai_h = ai_h;
    m_score_thresh = score_threshold;
    m_iou_thresh = iou_threshold;

    // Load MNN model from file
    m_interpreter.reset(MNN::Interpreter::createFromFile(model_path));
    if (!m_interpreter) return MNN_FACE_ERR_MODEL;

    // Configure MNN for embedded CPU (low precision, single thread)
    MNN::ScheduleConfig config;
    config.numThread = m_num_thread;
    MNN::BackendConfig backend_config;
    backend_config.precision = MNN::BackendConfig::Precision_Low;
    config.backendConfig = &backend_config;

    // Create MNN inference session
    m_session = m_interpreter->createSession(config);
    m_input_tensor = m_interpreter->getSessionInput(m_session, nullptr);

    // Pre-generate anchor prior boxes (one-time calculation)
    m_priors.clear();
    for (int i = 0; i < 4; i++) {
        float scale_w = (float)m_ai_w / m_strides[i];
        float scale_h = (float)m_ai_h / m_strides[i];
        int fm_w = ceil(m_ai_w / m_strides[i]);
        int fm_h = ceil(m_ai_h / m_strides[i]);

        for (int h = 0; h < fm_h; h++) {
            for (int w = 0; w < fm_w; w++) {
                float x_center = (w + 0.5f) / scale_w;
                float y_center = (h + 0.5f) / scale_h;
                for (float k : m_min_boxes[i]) {
                    float pw = k / m_ai_w;
                    float ph = k / m_ai_h;
                    m_priors.push_back({clip(x_center,1), clip(y_center,1), clip(pw,1), clip(ph,1)});
                }
            }
        }
    }
    m_num_anchors = m_priors.size();
    m_ready = true;
    return MNN_FACE_OK;
}

// ==============================================================================
// Public: Core Face Detection Inference
// ==============================================================================
int UltraFaceMNN::detect(const uint8_t* input_data, 
                         int cam_w, 
                         int cam_h,
                         uint8_t* external_rgb_buf,
                         std::vector<FaceInfo_MNN>& face_list,
                         ImageFormat format) {
    if (!m_ready || !input_data || !external_rgb_buf) {
        return MNN_FACE_ERR_INPUT;
    }
    face_list.clear();

    int conv_ret = MNN_FACE_OK;
    // ---------------------- Image Format Conversion (libyuv) ----------------------
    if (format == IMAGE_FORMAT_YUYV) {
        conv_ret = yuyv_to_rgb(input_data, cam_w, cam_h, external_rgb_buf);
    } else if (format == IMAGE_FORMAT_MJPEG) {
        // Fixed buffer length for embedded camera compatibility
        conv_ret = mjpeg_to_rgb(input_data, cam_w * cam_h * 2, cam_w, cam_h, external_rgb_buf);
    } else {
        return MNN_FACE_ERR_INPUT;
    }

    if (conv_ret != MNN_FACE_OK) {
        return conv_ret;
    }

    // ---------------------- RGB Image Resizing (libyuv hardware acceleration) ----------------------
    uint8_t* ai_img_buf = new uint8_t[m_ai_w * m_ai_h * 3];
    conv_ret = rgb_resize(external_rgb_buf, cam_w, cam_h, ai_img_buf, m_ai_w, m_ai_h);
    if (conv_ret != 0) {
        delete[] ai_img_buf;
        return MNN_FACE_ERR_INPUT;
    }

    // ---------------------- MNN Image Preprocessing ----------------------
    m_interpreter->resizeSession(m_session);
    std::shared_ptr<MNN::CV::ImageProcess> pretreat(
        MNN::CV::ImageProcess::create(MNN::CV::RGB, MNN::CV::RGB, m_mean_vals, 3, m_norm_vals, 3)
    );
    // Raw buffer input, no third-party library dependencies
    pretreat->convert(ai_img_buf, m_ai_w, m_ai_h, m_ai_w * 3, m_input_tensor);

    // Run MNN model inference
    m_interpreter->runSession(m_session);

    // Get output tensors (scores & bounding boxes)
    MNN::Tensor* tensor_scores = m_interpreter->getSessionOutput(m_session, "scores");
    MNN::Tensor* tensor_boxes  = m_interpreter->getSessionOutput(m_session, "boxes");

    // Copy tensor data to host memory
    MNN::Tensor scores_host(tensor_scores, tensor_scores->getDimensionType());
    MNN::Tensor boxes_host(tensor_boxes, tensor_boxes->getDimensionType());
    tensor_scores->copyToHostTensor(&scores_host);
    tensor_boxes->copyToHostTensor(&boxes_host);

    // ---------------------- Post-Processing ----------------------
    std::vector<FaceInfo_MNN> bbox_collection;
    // Generate candidate bounding boxes
    generate_bbox(bbox_collection, &scores_host, &boxes_host);
    // NMS suppression to remove duplicate boxes
    nms(bbox_collection, face_list);

    // Release temporary inference buffer
    delete[] ai_img_buf;

    return MNN_FACE_OK;
}

// ==============================================================================
// Private: Generate Candidate Bounding Boxes
// ==============================================================================
void UltraFaceMNN::generate_bbox(std::vector<FaceInfo_MNN>& bbox_collection, MNN::Tensor* scores, MNN::Tensor* boxes) {
    for (int i = 0; i < m_num_anchors; i++) {
        float score = scores->host<float>()[i * 2 + 1];
        // Filter low-confidence boxes
        if (score <= m_score_thresh) continue;

        FaceInfo_MNN face;
        // Decode box coordinates using anchor priors & variance
        float xc = boxes->host<float>()[i*4]   * m_center_variance * m_priors[i][2] + m_priors[i][0];
        float yc = boxes->host<float>()[i*4+1] * m_center_variance * m_priors[i][3] + m_priors[i][1];
        float w  = exp(boxes->host<float>()[i*4+2] * m_size_variance) * m_priors[i][2];
        float h  = exp(boxes->host<float>()[i*4+3] * m_size_variance) * m_priors[i][3];

        // Clip coordinates to valid range and scale to model size
        face.x1 = clip(xc - w/2, 1) * m_ai_w;
        face.y1 = clip(yc - h/2, 1) * m_ai_h;
        face.x2 = clip(xc + w/2, 1) * m_ai_w;
        face.y2 = clip(yc + h/2, 1) * m_ai_h;
        face.score = score;
        bbox_collection.push_back(face);
    }
}

// ==============================================================================
// Private: Non-Maximum Suppression (NMS)
// ==============================================================================
void UltraFaceMNN::nms(std::vector<FaceInfo_MNN>& input, std::vector<FaceInfo_MNN>& output) {
    if (input.empty()) return;

    // Sort boxes by confidence score (descending)
    std::sort(input.begin(), input.end(), [](const FaceInfo_MNN& a, const FaceInfo_MNN& b) {
        return a.score > b.score;
    });

    std::vector<int> merged(input.size(), 0);
    for (int i = 0; i < input.size(); i++) {
        if (merged[i]) continue;
        output.push_back(input[i]);
        merged[i] = 1;

        // Calculate IOU with subsequent boxes
        float area0 = (input[i].x2 - input[i].x1) * (input[i].y2 - input[i].y1);
        for (int j = i+1; j < input.size(); j++) {
            if (merged[j]) continue;

            // Compute intersection area
            float ix1 = std::max(input[i].x1, input[j].x1);
            float iy1 = std::max(input[i].y1, input[j].y1);
            float ix2 = std::min(input[i].x2, input[j].x2);
            float iy2 = std::min(input[i].y2, input[j].y2);

            float iw = std::max(0.0f, ix2 - ix1);
            float ih = std::max(0.0f, iy2 - iy1);
            float iarea = iw * ih;
            float area1 = (input[j].x2 - input[j].x1) * (input[j].y2 - input[j].y1);
            float iou = iarea / (area0 + area1 - iarea);

            // Suppress overlapping boxes
            if (iou > m_iou_thresh) merged[j] = 1;
        }
    }
}

// ==============================================================================
// Public: Coordinate Mapping Utility
// ==============================================================================
void UltraFaceMNN::map_face_to_original(FaceInfo_MNN& face, int ai_w, int ai_h, int cam_w, int cam_h) {
    float scale_w = (float)cam_w / ai_w;
    float scale_h = (float)cam_h / ai_h;
    // Scale box coordinates to original image resolution
    face.x1 *= scale_w;
    face.y1 *= scale_h;
    face.x2 *= scale_w;
    face.y2 *= scale_h;
}

// ==============================================================================
// Public: Resource Release
// ==============================================================================
void UltraFaceMNN::deinit() {
    // Safe release of MNN resources
    if (m_interpreter && m_session) {
        m_interpreter->releaseSession(m_session);
        m_interpreter->releaseModel();
    }
    // Reset all pointers and state
    m_session = nullptr;
    m_input_tensor = nullptr;
    m_ready = false;
}