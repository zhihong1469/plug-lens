```shell
luo@Luo1469:~/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/libjpeg-turbo-2.1.5$ grep "option" CMakeLists.txt 
option(ENABLE_SHARED "Build shared libraries" TRUE)
option(ENABLE_STATIC "Build static libraries" TRUE)
option(REQUIRE_SIMD "Generate a fatal error if SIMD extensions are not available for this platform (default is to fall back to a non-SIMD build)" FALSE)
option(WITH_12BIT "Encode/decode JPEG images with 12-bit samples (implies WITH_ARITH_DEC=0 WITH_ARITH_ENC=0 WITH_JAVA=0 WITH_SIMD=0 WITH_TURBOJPEG=0 )" FALSE)
option(WITH_ARITH_DEC "Include arithmetic decoding support when emulating the libjpeg v6b API/ABI" TRUE)
option(WITH_ARITH_ENC "Include arithmetic encoding support when emulating the libjpeg v6b API/ABI" TRUE)
  option(WITH_JAVA "Build Java wrapper for the TurboJPEG API library (implies ENABLE_SHARED=1)" FALSE)
option(WITH_JPEG7 "Emulate libjpeg v7 API/ABI (this makes ${CMAKE_PROJECT_NAME} backward-incompatible with libjpeg v6b)" FALSE)
option(WITH_JPEG8 "Emulate libjpeg v8 API/ABI (this makes ${CMAKE_PROJECT_NAME} backward-incompatible with libjpeg v6b)" FALSE)
option(WITH_MEM_SRCDST "Include in-memory source/destination manager functions when emulating the libjpeg v6b or v7 API/ABI" TRUE)
option(WITH_SIMD "Include SIMD extensions, if available for this platform" TRUE)
option(WITH_TURBOJPEG "Include the TurboJPEG API library and associated test programs" TRUE)
option(WITH_FUZZ "Build fuzz targets" FALSE)
macro(report_option var desc)
report_option(ENABLE_SHARED "Shared libraries")
report_option(ENABLE_STATIC "Static libraries")
report_option(WITH_12BIT "12-bit JPEG support")
  report_option(WITH_ARITH_DEC "Arithmetic decoding support")
  report_option(WITH_ARITH_ENC "Arithmetic encoding support")
  report_option(WITH_TURBOJPEG "TurboJPEG API library")
  report_option(WITH_JAVA "TurboJPEG Java wrapper")
  report_option(WITH_MEM_SRCDST "In-memory source/destination managers")
  option(WITH_CRT_DLL
option(FORCE_INLINE "Force function inlining" TRUE)
  # compiler and compiler options.  We leave it to the user to set FLOATTEST

```




cd /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/libjpeg-turbo-2.1.5
# 清理残留构建文件
rm -rf build
mkdir build && cd build

cmake .. \
-DCMAKE_C_COMPILER=/usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-gcc \
-DCMAKE_CXX_COMPILER=/usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/bin/arm-buildroot-linux-gnueabihf-g++ \
-DCMAKE_INSTALL_PREFIX=/home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/third_lib/libjpeg_turbo \
-DWITH_SIMD=OFF \
-DENABLE_STATIC=TRUE \
-DENABLE_SHARED=TRUE \
-DWITH_TURBOJPEG=TRUE \
-DWITH_MEM_SRCDST=TRUE \
-DWITH_ARITH_DEC=TRUE \
-DWITH_ARITH_ENC=TRUE \
-DWITH_12BIT=FALSE \
-DWITH_JAVA=FALSE \
-DWITH_JPEG7=FALSE \
-DWITH_JPEG8=FALSE

make -j4
make install


# libjpeg-turbo 路径配置
JPEG_TURBO_DIR := /home/luo/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/third_lib/libjpeg_turbo

# 头文件路径
CFLAGS += -I$(JPEG_TURBO_DIR)/include

# 库文件路径 + 链接库
LDFLAGS += -L$(JPEG_TURBO_DIR)/lib -lturbojpeg