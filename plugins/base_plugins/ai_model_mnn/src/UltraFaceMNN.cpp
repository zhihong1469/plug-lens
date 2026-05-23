#include "UltraFaceMNN.hpp"
#include <string.h>
#include <algorithm>
#include <math.h>
#include "img_joint.h"

#define clip(x, y) (x < 0 ? 0 : (x > y ? y : x))

UltraFaceMNN::UltraFaceMNN() 
    : m_session(nullptr), m_input_tensor(nullptr), m_ready(false),
      m_num_thread(DEFAULT_NUM_THREAD), 
      m_score_thresh(DEFAULT_SCORE_THRESH),
      m_iou_thresh(DEFAULT_IOU_THRESH)
{
    m_min_boxes = {{10.0f,16.0f,24.0f}, {32.0f,48.0f}, {64.0f,96.0f}, {128.0f,192.0f,256.0f}};
    m_strides = {8.0f, 16.0f, 32.0f, 64.0f};
}

UltraFaceMNN::~UltraFaceMNN() {
    deinit();
}

int UltraFaceMNN::init(const char* model_path, int ai_w, int ai_h,
                       float score_threshold, float iou_threshold) {
    if (!model_path) return MNN_FACE_ERR_INPUT;

    m_ai_w = ai_w;
    m_ai_h = ai_h;
    m_score_thresh = score_threshold;
    m_iou_thresh = iou_threshold;

    m_interpreter.reset(MNN::Interpreter::createFromFile(model_path));
    if (!m_interpreter) return MNN_FACE_ERR_MODEL;

    MNN::ScheduleConfig config;
    config.numThread = m_num_thread;
    MNN::BackendConfig backend_config;
    backend_config.precision = MNN::BackendConfig::Precision_Low;
    config.backendConfig = &backend_config;

    m_session = m_interpreter->createSession(config);
    m_input_tensor = m_interpreter->getSessionInput(m_session, nullptr);

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


// =============================================================================
// 核心重构：通用版detect函数，支持YUYV/MJPEG双格式自动切换
// =============================================================================
int UltraFaceMNN::detect(const uint8_t* input_data, 
                         int cam_w, 
                         int cam_h,
                         uint8_t* external_bgr_buf,
                         std::vector<FaceInfo_MNN>& face_list,
                         ImageFormat format) {
    if (!m_ready || !input_data || !external_bgr_buf) {
        return MNN_FACE_ERR_INPUT;
    }
    face_list.clear();

    int conv_ret = MNN_FACE_OK;
    // ====================== 格式转换：自动切换 ======================
    if (format == IMAGE_FORMAT_YUYV) {
        // 调用全局通用函数，替换原来的成员函数
        conv_ret = yuyv_to_bgr(input_data, cam_w, cam_h, external_bgr_buf);
    } else if (format == IMAGE_FORMAT_MJPEG) {
        // 调用全局通用函数，替换原来的成员函数
        conv_ret = mjpeg_to_bgr(input_data, cam_w * cam_h * 2, cam_w, cam_h, external_bgr_buf);
    } else {
        return MNN_FACE_ERR_INPUT; // 不支持的格式
    }

    if (conv_ret != MNN_FACE_OK) {
        return conv_ret;
    }

    // 包裹外部缓冲区，不拷贝内存
    cv::Mat bgr_img(cam_h, cam_w, CV_8UC3, external_bgr_buf);
    cv::Mat ai_img;
    cv::resize(bgr_img, ai_img, cv::Size(m_ai_w, m_ai_h));

    // 模型预处理
    m_interpreter->resizeSession(m_session);
    std::shared_ptr<MNN::CV::ImageProcess> pretreat(
        MNN::CV::ImageProcess::create(MNN::CV::BGR, MNN::CV::RGB, m_mean_vals, 3, m_norm_vals, 3)
    );
    pretreat->convert(ai_img.data, m_ai_w, m_ai_h, ai_img.step[0], m_input_tensor);

    // 推理
    m_interpreter->runSession(m_session);

    // 获取输出
    MNN::Tensor* tensor_scores = m_interpreter->getSessionOutput(m_session, "scores");
    MNN::Tensor* tensor_boxes  = m_interpreter->getSessionOutput(m_session, "boxes");

    MNN::Tensor scores_host(tensor_scores, tensor_scores->getDimensionType());
    MNN::Tensor boxes_host(tensor_boxes, tensor_boxes->getDimensionType());
    tensor_scores->copyToHostTensor(&scores_host);
    tensor_boxes->copyToHostTensor(&boxes_host);

    // 后处理
    std::vector<FaceInfo_MNN> bbox_collection;
    generate_bbox(bbox_collection, &scores_host, &boxes_host);
    nms(bbox_collection, face_list);

    return MNN_FACE_OK;
}

void UltraFaceMNN::generate_bbox(std::vector<FaceInfo_MNN>& bbox_collection, MNN::Tensor* scores, MNN::Tensor* boxes) {
    for (int i = 0; i < m_num_anchors; i++) {
        float score = scores->host<float>()[i * 2 + 1];
        if (score <= m_score_thresh) continue;

        FaceInfo_MNN face;
        float xc = boxes->host<float>()[i*4]   * m_center_variance * m_priors[i][2] + m_priors[i][0];
        float yc = boxes->host<float>()[i*4+1] * m_center_variance * m_priors[i][3] + m_priors[i][1];
        float w  = exp(boxes->host<float>()[i*4+2] * m_size_variance) * m_priors[i][2];
        float h  = exp(boxes->host<float>()[i*4+3] * m_size_variance) * m_priors[i][3];

        face.x1 = clip(xc - w/2, 1) * m_ai_w;
        face.y1 = clip(yc - h/2, 1) * m_ai_h;
        face.x2 = clip(xc + w/2, 1) * m_ai_w;
        face.y2 = clip(yc + h/2, 1) * m_ai_h;
        face.score = score;
        bbox_collection.push_back(face);
    }
}

void UltraFaceMNN::nms(std::vector<FaceInfo_MNN>& input, std::vector<FaceInfo_MNN>& output) {
    if (input.empty()) return;
    std::sort(input.begin(), input.end(), [](const FaceInfo_MNN& a, const FaceInfo_MNN& b) {
        return a.score > b.score;
    });

    std::vector<int> merged(input.size(), 0);
    for (int i = 0; i < input.size(); i++) {
        if (merged[i]) continue;
        output.push_back(input[i]);
        merged[i] = 1;

        float area0 = (input[i].x2 - input[i].x1) * (input[i].y2 - input[i].y1);
        for (int j = i+1; j < input.size(); j++) {
            if (merged[j]) continue;
            float ix1 = std::max(input[i].x1, input[j].x1);
            float iy1 = std::max(input[i].y1, input[j].y1);
            float ix2 = std::min(input[i].x2, input[j].x2);
            float iy2 = std::min(input[i].y2, input[j].y2);

            float iw = std::max(0.0f, ix2 - ix1);
            float ih = std::max(0.0f, iy2 - iy1);
            float iarea = iw * ih;
            float area1 = (input[j].x2 - input[j].x1) * (input[j].y2 - input[j].y1);
            float iou = iarea / (area0 + area1 - iarea);

            if (iou > m_iou_thresh) merged[j] = 1;
        }
    }
}

void UltraFaceMNN::map_face_to_original(FaceInfo_MNN& face, int ai_w, int ai_h, int cam_w, int cam_h) {
    float scale_w = (float)cam_w / ai_w;
    float scale_h = (float)cam_h / ai_h;
    face.x1 *= scale_w;
    face.y1 *= scale_h;
    face.x2 *= scale_w;
    face.y2 *= scale_h;
}

void UltraFaceMNN::draw_faces(cv::Mat& img, const std::vector<FaceInfo_MNN>& face_list) {
    for (const auto& face : face_list) {
        cv::rectangle(img, cv::Point((int)face.x1, (int)face.y1), 
                      cv::Point((int)face.x2, (int)face.y2), cv::Scalar(0,255,0), 2);
    }
}

void UltraFaceMNN::deinit() {
    if (m_interpreter && m_session) {
        m_interpreter->releaseSession(m_session);
        m_interpreter->releaseModel();
    }
    m_session = nullptr;
    m_input_tensor = nullptr;
    m_ready = false;
}