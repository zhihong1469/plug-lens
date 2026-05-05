#include "UltraFace.hpp"
#include <iostream>
#include <vector>
#include <opencv2/opencv.hpp>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <model.mnn> <image_path>" << std::endl;
        return 1;
    }

    // 初始化模型
    UltraFace ultraface(argv[1], 320, 240, 1, 0.65f, 0.5f);

    // 读取图片
    cv::Mat img = cv::imread(argv[2]);
    if (img.empty()) {
        std::cout << "Failed to read image!" << std::endl;
        return -1;
    }

    // 人脸检测
    std::vector<FaceInfo> face_list;
    ultraface.detect(img, face_list);

    // 打印结果
    std::cout << "Detect " << face_list.size() << " faces" << std::endl;
    for (auto& face : face_list) {
        std::cout << "Face: [" << face.x1 << "," << face.y1 << "," << face.x2 << "," << face.y2 << "] score: " << face.score << std::endl;
    }

    return 0;
}