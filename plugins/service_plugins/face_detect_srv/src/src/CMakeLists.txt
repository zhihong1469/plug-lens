cmake_minimum_required(VERSION 3.10)
project(Ultra-face-mnn)

# 交叉编译工具链（你的buildroot路径）
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER /usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER /usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-g++)

# ARM优化
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=armv7-a -mfpu=neon -mfloat-abi=hard")
set(CMAKE_CXX_STANDARD 11)

# 头文件&库
include_directories(mnn/include src)
link_directories(mnn/lib)

# 编译
add_executable(Ultra-face-mnn src/main.cpp src/UltraFace.cpp)
target_link_libraries(Ultra-face-mnn MNN pthread m)