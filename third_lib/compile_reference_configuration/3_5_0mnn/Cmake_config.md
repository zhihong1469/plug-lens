- https://github.com/alibaba/MNN  【3.5.0】
- https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB
- https://github.com/opencv/opencv 【4.5.5】
- 现成 INT8 模型版本和 MNN3.5.0 不兼容
cd /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/MNN-master/build_arm
rm -rf *
# 编译宏介绍:https://mnn-docs.readthedocs.io/en/latest/compile/cmake.html?highlight=mnn_build_mini
cmake .. \
-DCMAKE_SYSTEM_NAME=Linux \
-DCMAKE_SYSTEM_PROCESSOR=armv7-a \
-DCMAKE_C_COMPILER=/usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-gcc \
-DCMAKE_CXX_COMPILER=/usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-g++ \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_C_FLAGS="-march=armv7-a -mfloat-abi=hard -mfpu=neon" \
-DCMAKE_CXX_FLAGS="-march=armv7-a -mfloat-abi=hard -mfpu=neon" \
-DMNN_BUILD_MINI=ON \
-DMNN_REDUCE_SIZE=OFF \
-DMNN_SKIPBUILD_GEOMETRY=OFF \
-DMNN_SUPPORT_QUANT_EXTEND=ON \
-DMNN_SEP_BUILD=OFF \
-DMNN_ARM82=OFF \
-DMNN_KLEIDIAI=OFF \
-DMNN_BUILD_SHARED_LIBS=ON \
-DMNN_BUILD_TEST=OFF \
-DMNN_BUILD_TOOLS=OFF \
-DMNN_USE_THREAD_POOL=ON

make -j12
ls -lh
# 以上配置：
# DMNN_BUILD_MINI---关正常编译约3.4mb---arm-buildroot-linux-gnueabihf-strip libMNN.so---极限2673k
# DMNN_BUILD_MINI---开约1.6mb---arm-buildroot-linux-gnueabihf-strip libMNN.so---极限1190k

cp -f libMNN.so /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/Ultra-Light-Fast-Generic-Face-Detector-1MB-master/MNN/mnn/lib/


cd /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/opencv-4.5.5
mkdir build_arm && cd build_arm
cmake .. \
-DCMAKE_SYSTEM_NAME=Linux \
-DCMAKE_SYSTEM_PROCESSOR=arm \
-DCMAKE_C_COMPILER=/usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-gcc \
-DCMAKE_CXX_COMPILER=/usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-g++ \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_INSTALL_PREFIX=/home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/opencv-4.5.5/install_arm \
-DENABLE_NEON=ON \
-DCPU_BASELINE=NEON \
-DBUILD_SHARED_LIBS=ON \
-DBUILD_opencv_core=ON \
-DBUILD_opencv_imgproc=ON \
-DBUILD_opencv_imgcodecs=ON \
-DBUILD_opencv_highgui=OFF \
-DBUILD_opencv_videoio=OFF \
-DBUILD_opencv_objdetect=OFF \
-DBUILD_opencv_python=OFF \
-DBUILD_opencv_java=OFF \
-DBUILD_opencv_apps=OFF \
-DBUILD_EXAMPLES=OFF \
-DBUILD_TESTS=OFF \
-DBUILD_PERF_TESTS=OFF \
-DWITH_JPEG=ON \
-DWITH_PNG=ON \
-DWITH_TIFF=OFF \
-DWITH_WEBP=OFF \
-DWITH_FFMPEG=OFF \
-DWITH_GSTREAMER=OFF \
-DWITH_GTK=OFF \
-DWITH_QT=OFF \
-DWITH_CUDA=OFF \
-DWITH_OPENCL=OFF \
-DWITH_OPENMP=OFF
# 4. 编译安装
make -j12
make install
cp -rf  /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/opencv-4.5.5/install_arm  ~/nfs/run_on_board/


cd /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/Ultra-Light-Fast-Generic-Face-Detector-1MB-master/MNN/build_arm
rm -rf *
cmake .. \
-DCMAKE_SYSTEM_NAME=Linux \
-DCMAKE_SYSTEM_PROCESSOR=arm \
-DCMAKE_C_COMPILER=/usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-gcc \
-DCMAKE_CXX_COMPILER=/usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-g++ \
-DOpenCV_DIR=/home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/opencv-4.5.5/install_arm/lib/cmake/opencv4

make -j12


cd /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/Ultra-Light-Fast-Generic-Face-Detector-1MB-master/MNN
# 创建一个独立文件夹，存放所有要放到开发板的文件
mkdir run_on_board && cd run_on_board

# 1. 复制 可执行程序（核心）
cp -f /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/Ultra-Light-Fast-Generic-Face-Detector-1MB-master/MNN/build_arm/Ultra-face-mnn  /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/third_lib/face_detector

# 2. 复制 推理库
cp -f /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/Ultra-Light-Fast-Generic-Face-Detector-1MB-master/MNN/mnn/lib/libMNN.so /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/third_lib/face_detector

# 3. 复制 人脸检测模型（用最稳定的RFB-320 FP32模型）
cp -rf /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/Ultra-Light-Fast-Generic-Face-Detector-1MB-master/MNN/model  /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/third_lib/face_detector

# 4. 复制 测试图片
cp -rf /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/Ultra-Light-Fast-Generic-Face-Detector-1MB-master/MNN/imgs   /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/third_lib/face_detector

cp -rf  /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/third_lib/face_detector ~/nfs


