#include "UltraFace.hpp"
#include <iostream>
#include <opencv2/opencv.hpp>

using namespace std;

int main(int argc, char **argv) {
    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <mnn .mnn> [image files...]\n", argv[0]);
        return 1;
    }

    string mnn_path = argv[1];
    // 🔥 修复：单核CPU，线程数必须改为 1
    UltraFace ultraface(mnn_path, 320, 240, 1, 0.65); 

    for (int i = 2; i < argc; i++) {
        string image_file = argv[i];
        cout << "Processing " << argv[i] << endl;

        cv::Mat frame = cv::imread(image_file);
        vector<FaceInfo> face_info;
        
        try {
            ultraface.detect(frame, face_info);
        } catch (...) {
            cout << "Detect error" << endl;
            continue;
        }

        // 绘制框
        for (auto face : face_info) {
            cv::rectangle(frame, cv::Point(face.x1, face.y1), cv::Point(face.x2, face.y2), cv::Scalar(0,255,0), 2);
        }

        // 保存结果，不显示
        string result_name = "result.jpg";
        cv::imwrite(result_name, frame);
        cout << "检测完成！结果已保存：" << result_name << endl;
        cout << "发现人脸数量：" << face_info.size() << endl;
    }
    return 0;
}