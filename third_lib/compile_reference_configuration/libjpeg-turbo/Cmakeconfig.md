```shell
luo@Luo1469:~/linux/6ull/project/peripheral_vision_ai_acquisition_terminal/.tool/libjpeg-turbo-3.1.4.1$ grep "option" CMakeLists.txt 
# 3. optionally provide a way to skip the installation of libjpeg-turbo
# 4. optionally provide a way to postfix target names, to avoid namespace
option(ENABLE_SHARED "Build shared libraries" TRUE)
option(ENABLE_STATIC "Build static libraries" TRUE)
option(REQUIRE_SIMD
option(WITH_ARITH_DEC
option(WITH_ARITH_ENC
  option(WITH_JAVA
option(WITH_JPEG7
option(WITH_JPEG8
option(WITH_SIMD "Include SIMD extensions, if available for this platform"
option(WITH_TURBOJPEG
option(WITH_TOOLS
option(WITH_TESTS "Enable regression tests, and build associated test programs (WARNING: Disable this at your own risk. If a build is not validated with the regression tests, then it is the responsibility of the builder to ensure, via other means, that the build produces mathematically correct results.)"
option(WITH_FUZZ "Build fuzz targets" FALSE)
macro(report_option var desc)
report_option(ENABLE_SHARED "Shared libraries")
report_option(ENABLE_STATIC "Static libraries")
report_option(WITH_ARITH_DEC "Arithmetic decoding support")
report_option(WITH_ARITH_ENC "Arithmetic encoding support")
report_option(WITH_TURBOJPEG "TurboJPEG API library")
report_option(WITH_JAVA "TurboJPEG Java API")
report_option(WITH_TOOLS "Command-line tools")
report_option(WITH_TESTS "Regression tests")
  option(WITH_CRT_DLL
option(FORCE_INLINE "Force function inlining" TRUE)
# depending on the compiler and compiler options.  We leave it to the user to
# depending on the compiler and compiler options.  We leave it to the user to

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


# 库文件路径 + 链接库
LDFLAGS += -L$(JPEG_TURBO_DIR)/lib -lturbojpeg