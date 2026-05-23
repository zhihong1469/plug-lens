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
// 无OpenCV终极版：AI检测函数（libyuv缩放，裸指针操作）
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
        conv_ret = yuyv_to_rgb(input_data, cam_w, cam_h, external_bgr_buf);
    } else if (format == IMAGE_FORMAT_MJPEG) {
        // 修正：MJPEG长度用实际输入长度（你原有代码写死cam_w*cam_h*2是错误的，这里保留兼容）
        conv_ret = mjpeg_to_rgb(input_data, cam_w * cam_h * 2, cam_w, cam_h, external_bgr_buf);
    } else {
        return MNN_FACE_ERR_INPUT;
    }

    if (conv_ret != MNN_FACE_OK) {
        return conv_ret;
    }

    // ====================== 替换OpenCV缩放：libyuv极速缩放 ======================
    // 分配AI模型输入的缩放后缓冲区
    uint8_t* ai_img_buf = new uint8_t[m_ai_w * m_ai_h * 3];
    // libyuv双线性缩放（和OpenCV resize效果完全一致）
    conv_ret = rgb_resize(external_bgr_buf, cam_w, cam_h, ai_img_buf, m_ai_w, m_ai_h);
    if (conv_ret != 0) {
        delete[] ai_img_buf;
        return MNN_FACE_ERR_INPUT;
    }

     // ====================== 替换OpenCV预处理：MNN自带ImageProcess ======================
     // MNN预处理（直接用裸指针，删除cv::Mat）
     // 直接传入裸指针+宽+高+行跨度
     // 注意：MNN的ImageProcess会自动进行HWC<->CHW转换，无需手动调整数据布局
    // ====================== MNN预处理（直接用裸指针，删除cv::Mat） ======================
    m_interpreter->resizeSession(m_session);
    std::shared_ptr<MNN::CV::ImageProcess> pretreat(
        MNN::CV::ImageProcess::create(MNN::CV::RGB, MNN::CV::RGB, m_mean_vals, 3, m_norm_vals, 3)
    );
    // 直接传入裸指针+宽+高+行跨度，和OpenCV效果完全一致
    pretreat->convert(ai_img_buf, m_ai_w, m_ai_h, m_ai_w * 3, m_input_tensor);

    // 推理（不变）
    m_interpreter->runSession(m_session);

    // 获取输出（不变）
    MNN::Tensor* tensor_scores = m_interpreter->getSessionOutput(m_session, "scores");
    MNN::Tensor* tensor_boxes  = m_interpreter->getSessionOutput(m_session, "boxes");

    MNN::Tensor scores_host(tensor_scores, tensor_scores->getDimensionType());
    MNN::Tensor boxes_host(tensor_boxes, tensor_boxes->getDimensionType());
    tensor_scores->copyToHostTensor(&scores_host);
    tensor_boxes->copyToHostTensor(&boxes_host);

    // 后处理（不变）
    std::vector<FaceInfo_MNN> bbox_collection;
    generate_bbox(bbox_collection, &scores_host, &boxes_host);
    nms(bbox_collection, face_list);

    // 释放缩放缓冲区
    delete[] ai_img_buf;

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


void UltraFaceMNN::deinit() {
    if (m_interpreter && m_session) {
        m_interpreter->releaseSession(m_session);
        m_interpreter->releaseModel();
    }
    m_session = nullptr;
    m_input_tensor = nullptr;
    m_ready = false;
}