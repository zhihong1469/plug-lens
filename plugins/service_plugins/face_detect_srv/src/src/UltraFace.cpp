#define clip(x, y) (x < 0 ? 0 : (x > y ? y : x))
#include "UltraFace.hpp"
#include <cstdlib> // 新增malloc头文件
using namespace std;

UltraFace::UltraFace(const std::string &mnn_path,
                     int input_width, int input_length, int num_thread_,
                     float score_threshold_, float iou_threshold_, int topk_) {
    num_thread = num_thread_;
    score_threshold = score_threshold_;
    iou_threshold = iou_threshold_;
    in_w = input_width;
    in_h = input_length;
    w_h_list = {in_w, in_h};

    for (auto size : w_h_list) {
        std::vector<float> fm_item;
        for (float stride : strides) {
            fm_item.push_back(ceil(size / stride));
        }
        featuremap_size.push_back(fm_item);
    }
    for (auto size : w_h_list) {
        shrinkage_size.push_back(strides);
    }

    for (int index = 0; index < num_featuremap; index++) {
        float scale_w = in_w / shrinkage_size[0][index];
        float scale_h = in_h / shrinkage_size[1][index];
        for (int j = 0; j < featuremap_size[1][index]; j++) {
            for (int i = 0; i < featuremap_size[0][index]; i++) {
                float x_center = (i + 0.5) / scale_w;
                float y_center = (j + 0.5) / scale_h;
                for (float k : min_boxes[index]) {
                    float w = k / in_w;
                    float h = k / in_h;
                    priors.push_back({clip(x_center, 1), clip(y_center, 1), clip(w, 1), clip(h, 1)});
                }
            }
        }
    }
    num_anchors = priors.size();

    ultraface_interpreter = std::shared_ptr<MNN::Interpreter>(MNN::Interpreter::createFromFile(mnn_path.c_str()));
    MNN::ScheduleConfig config;
    config.numThread = 1;
    MNN::BackendConfig backendConfig;
    backendConfig.precision = MNN::BackendConfig::PrecisionMode(2);
    config.backendConfig = &backendConfig;
    ultraface_session = ultraface_interpreter->createSession(config);
    input_tensor = ultraface_interpreter->getSessionInput(ultraface_session, nullptr);
}

UltraFace::~UltraFace() {
    ultraface_interpreter->releaseModel();
    ultraface_interpreter->releaseSession(ultraface_session);
}

void UltraFace::yuyv_to_rgb(const unsigned char* yuyv, unsigned char* rgb, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 2) {
            int idx = (y * w + x) * 2;
            unsigned char Y0 = yuyv[idx];
            unsigned char U  = yuyv[idx+1];
            unsigned char Y1 = yuyv[idx+2];
            unsigned char V  = yuyv[idx+3];

            int R0 = Y0 + 1.402f * (V - 128);
            int G0 = Y0 - 0.344f * (U - 128) - 0.714f * (V - 128);
            int B0 = Y0 + 1.772f * (U - 128);
            int R1 = Y1 + 1.402f * (V - 128);
            int G1 = Y1 - 0.344f * (U - 128) - 0.714f * (V - 128);
            int B1 = Y1 + 1.772f * (U - 128);

            R0 = std::max(0, std::min(255, R0));
            G0 = std::max(0, std::min(255, G0));
            B0 = std::max(0, std::min(255, B0));
            R1 = std::max(0, std::min(255, R1));
            G1 = std::max(0, std::min(255, G1));
            B1 = std::max(0, std::min(255, B1));

            int pos = (y * w + x) * 3;
            rgb[pos]   = R0; rgb[pos+1] = G0; rgb[pos+2] = B0;
            rgb[pos+3] = R1; rgb[pos+4] = G1; rgb[pos+5] = B1;
        }
    }
}

int UltraFace::detect(const unsigned char* yuyv_320x240, std::vector<FaceInfo> &face_list) {
    image_w = in_w;
    image_h = in_h;

    // ✅ 修复：堆分配RGB数据，彻底解决栈溢出
    unsigned char* rgb_data = (unsigned char*)malloc(320*240*3);
    yuyv_to_rgb(yuyv_320x240, rgb_data, 320, 240);

    ultraface_interpreter->resizeTensor(input_tensor, {1, 3, in_h, in_w});
    ultraface_interpreter->resizeSession(ultraface_session);

    float* input_ptr = input_tensor->host<float>();
    for (int i = 0; i < 320*240*3; i++) {
        input_ptr[i] = (rgb_data[i] - mean_vals[i%3]) * norm_vals[i%3];
    }

    ultraface_interpreter->runSession(ultraface_session);

    MNN::Tensor *tensor_scores = ultraface_interpreter->getSessionOutput(ultraface_session, "scores");
    MNN::Tensor *tensor_boxes = ultraface_interpreter->getSessionOutput(ultraface_session, "boxes");

    MNN::Tensor tensor_scores_host(tensor_scores, tensor_scores->getDimensionType());
    tensor_scores->copyToHostTensor(&tensor_scores_host);
    MNN::Tensor tensor_boxes_host(tensor_boxes, tensor_boxes->getDimensionType());
    tensor_boxes->copyToHostTensor(&tensor_boxes_host);

    std::vector<FaceInfo> bbox_collection;
    generateBBox(bbox_collection, tensor_scores, tensor_boxes);
    nms(bbox_collection, face_list);

    // ✅ 修复：释放堆内存
    free(rgb_data);
    return 0;
}

void UltraFace::generateBBox(std::vector<FaceInfo> &bbox_collection, MNN::Tensor *scores, MNN::Tensor *boxes) {
    for (int i = 0; i < num_anchors; i++) {
        if (scores->host<float>()[i * 2 + 1] > score_threshold) {
            FaceInfo rects;
            float x_center = boxes->host<float>()[i * 4] * center_variance * priors[i][2] + priors[i][0];
            float y_center = boxes->host<float>()[i * 4 + 1] * center_variance * priors[i][3] + priors[i][1];
            float w = exp(boxes->host<float>()[i * 4 + 2] * size_variance) * priors[i][2];
            float h = exp(boxes->host<float>()[i * 4 + 3] * size_variance) * priors[i][3];

            rects.x1 = clip(x_center - w / 2.0, 1) * image_w;
            rects.y1 = clip(y_center - h / 2.0, 1) * image_h;
            rects.x2 = clip(x_center + w / 2.0, 1) * image_w;
            rects.y2 = clip(y_center + h / 2.0, 1) * image_h;
            rects.score = clip(scores->host<float>()[i * 2 + 1], 1);
            bbox_collection.push_back(rects);
        }
    }
}

void UltraFace::nms(std::vector<FaceInfo> &input, std::vector<FaceInfo> &output, int type) {
    std::sort(input.begin(), input.end(), [](const FaceInfo &a, const FaceInfo &b) { return a.score > b.score; });
    int box_num = input.size();
    std::vector<int> merged(box_num, 0);

    for (int i = 0; i < box_num; i++) {
        if (merged[i]) continue;
        std::vector<FaceInfo> buf;
        buf.push_back(input[i]); merged[i] = 1;
        float h0 = input[i].y2 - input[i].y1 + 1;
        float w0 = input[i].x2 - input[i].x1 + 1;
        float area0 = h0 * w0;

        for (int j = i + 1; j < box_num; j++) {
            if (merged[j]) continue;
            float inner_x0 = std::max(input[i].x1, input[j].x1);
            float inner_y0 = std::max(input[i].y1, input[j].y1);
            float inner_x1 = std::min(input[i].x2, input[j].x2);
            float inner_y1 = std::min(input[i].y2, input[j].y2);
            float inner_h = inner_y1 - inner_y0 + 1;
            float inner_w = inner_x1 - inner_x0 + 1;

            if (inner_h <= 0 || inner_w <= 0) continue;
            float inner_area = inner_h * inner_w;
            float h1 = input[j].y2 - input[j].y1 + 1;
            float w1 = input[j].x2 - input[j].x1 + 1;
            float area1 = h1 * w1;
            float iou = inner_area / (area0 + area1 - inner_area);

            if (iou > iou_threshold) { merged[j] = 1; buf.push_back(input[j]); }
        }

        if (type == hard_nms) output.push_back(buf[0]);
        if (type == blending_nms) {
            float total = 0;
            for (auto &b : buf) total += exp(b.score);
            FaceInfo rects = {0};
            for (auto &b : buf) {
                float rate = exp(b.score) / total;
                rects.x1 += b.x1 * rate; rects.y1 += b.y1 * rate;
                rects.x2 += b.x2 * rate; rects.y2 += b.y2 * rate;
                rects.score += b.score * rate;
            }
            output.push_back(rects);
        }
    }
}