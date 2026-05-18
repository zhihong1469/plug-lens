#include "UltraFaceMNN.hpp"
#include <string.h>
#include <algorithm>
#include <math.h>

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

int UltraFaceMNN::yuyv_to_bgr(const uint8_t* yuyv_data, int width, int height, cv::Mat& out_img) {
    if (!yuyv_data || width <=0 || height <=0) {
        printf("[AI_MNN_ERROR] YUYV转换入参错误\n");
        return MNN_FACE_ERR_INPUT;
    }
    cv::Mat yuyv(height, width, CV_8UC2, (void*)yuyv_data);
    cv::cvtColor(yuyv, out_img, cv::COLOR_YUV2BGR_YUYV);

    // 调试日志
    printf("[AI_MNN_DEBUG] YUYV转换成功，输出图像数据指针=%p\n", out_img.data);
    return MNN_FACE_OK;
}

// 【修复】全部使用 FaceInfo_MNN
int UltraFaceMNN::detect(const uint8_t* yuyv_data, int cam_w, int cam_h, std::vector<FaceInfo_MNN>& face_list) {
    // ====================== 调试日志：初始检查 ======================
    printf("[AI_MNN_DEBUG] UltraFace 内部detect | 就绪状态=%d | 数据指针=%p\n",
           m_ready, yuyv_data);

    if (!m_ready || !yuyv_data) {
        printf("[AI_MNN_ERROR] 模型未就绪/数据空！返回 MNN_FACE_ERR_INPUT\n");
        return MNN_FACE_ERR_INPUT;
    }
    face_list.clear();

    cv::Mat bgr_img;
    // ====================== 调试日志：YUYV转BGR ======================
    int ret = yuyv_to_bgr(yuyv_data, cam_w, cam_h, bgr_img);
    printf("[AI_MNN_DEBUG] YUYV转BGR完成，返回码=%d | BGR图尺寸=%dx%d\n",
           ret, bgr_img.cols, bgr_img.rows);
    if (ret != MNN_FACE_OK) {
        printf("[AI_MNN_ERROR] YUYV转BGR失败！\n");
        return ret;
    }

    cv::Mat ai_img;
    // ====================== 调试日志：缩放图像 ======================
    cv::resize(bgr_img, ai_img, cv::Size(m_ai_w, m_ai_h));
    printf("[AI_MNN_DEBUG] 图像缩放完成 → 模型尺寸%dx%d\n", m_ai_w, m_ai_h);

    // 模型预处理
    m_interpreter->resizeSession(m_session);
    std::shared_ptr<MNN::CV::ImageProcess> pretreat(
        MNN::CV::ImageProcess::create(MNN::CV::BGR, MNN::CV::RGB, m_mean_vals, 3, m_norm_vals, 3)
    );
    pretreat->convert(ai_img.data, m_ai_w, m_ai_h, ai_img.step[0], m_input_tensor);

    // ====================== 调试日志：执行推理 ======================
    printf("[AI_MNN_DEBUG] 开始执行MNN模型推理...\n");
    m_interpreter->runSession(m_session);
    printf("[AI_MNN_DEBUG] MNN模型推理执行完成\n");

    // 获取输出张量
    MNN::Tensor* tensor_scores = m_interpreter->getSessionOutput(m_session, "scores");
    MNN::Tensor* tensor_boxes  = m_interpreter->getSessionOutput(m_session, "boxes");
    printf("[AI_MNN_DEBUG] 获取输出张量：scores=%p, boxes=%p\n", tensor_scores, tensor_boxes);

    MNN::Tensor scores_host(tensor_scores, tensor_scores->getDimensionType());
    MNN::Tensor boxes_host(tensor_boxes, tensor_boxes->getDimensionType());
    tensor_scores->copyToHostTensor(&scores_host);
    tensor_boxes->copyToHostTensor(&boxes_host);

    // 生成检测框 + NMS
    std::vector<FaceInfo_MNN> bbox_collection;
    generate_bbox(bbox_collection, &scores_host, &boxes_host);
    nms(bbox_collection, face_list);

    printf("[AI_MNN_DEBUG] 生成人脸框数量=%zu\n", face_list.size());
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