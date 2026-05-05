#ifndef UltraFace_hpp
#define UltraFace_hpp

#pragma once

#include "Interpreter.hpp"
#include "MNNDefine.h"
#include "Tensor.hpp"
#include "ImageProcess.hpp"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <string.h>
#include <math.h>

#define num_featuremap 4
#define hard_nms 1
#define blending_nms 2

typedef struct FaceInfo {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
} FaceInfo;

class UltraFace {
public:
    // 核心构造函数：完全保留原版逻辑
    UltraFace(const std::string &mnn_path,
              int input_width, int input_length, int num_thread_ = 1, float score_threshold_ = 0.7, float iou_threshold_ = 0.3,
              int topk_ = -1);

    ~UltraFace();

    // 关键修改：去掉cv::Mat，直接接收摄像头YUYV裸数据
    int detect(const unsigned char* yuyv_320x240, std::vector<FaceInfo> &face_list);

private:
    // 原版核心函数：锚框、NMS 完全保留，不动一行！
    void generateBBox(std::vector<FaceInfo> &bbox_collection, MNN::Tensor *scores, MNN::Tensor *boxes);
    void nms(std::vector<FaceInfo> &input, std::vector<FaceInfo> &output, int type = blending_nms);
    void yuyv_to_rgb(const unsigned char* yuyv, unsigned char* rgb, int w, int h);

private:
    std::shared_ptr<MNN::Interpreter> ultraface_interpreter;
    MNN::Session *ultraface_session = nullptr;
    MNN::Tensor *input_tensor = nullptr;

    int num_thread;
    int image_w;
    int image_h;
    int in_w;
    int in_h;
    int num_anchors;

    float score_threshold;
    float iou_threshold;

    // 原版模型预处理参数：必须保留！
    const float mean_vals[3] = {127, 127, 127};
    const float norm_vals[3] = {1.0 / 128, 1.0 / 128, 1.0 / 128};
    const float center_variance = 0.1;
    const float size_variance = 0.2;

    // 原版锚框参数：绝对不能改！
    const std::vector<std::vector<float>> min_boxes = {
            {10.0f,  16.0f,  24.0f},
            {32.0f,  48.0f},
            {64.0f,  96.0f},
            {128.0f, 192.0f, 256.0f}};
    const std::vector<float> strides = {8.0, 16.0, 32.0, 64.0};
    std::vector<std::vector<float>> featuremap_size;
    std::vector<std::vector<float>> shrinkage_size;
    std::vector<int> w_h_list;
    std::vector<std::vector<float>> priors = {};
};

#endif