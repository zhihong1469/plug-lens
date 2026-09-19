#!/bin/bash
# RGA SDK 编译脚本 - 使用 arm64-linux-75 工具链
# 用法: ./build_rga.sh [install]

set -e

# 工具链路径
TOOLCHAIN_PATH="/usr/local/arm/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu"
TOOLCHAIN_BIN="${TOOLCHAIN_PATH}/bin"
CROSS_COMPILE="${TOOLCHAIN_BIN}/aarch64-linux-gnu-"

# 检查工具链
if [ ! -f "${CROSS_COMPILE}gcc" ]; then
    echo "❌ 错误: 找不到 arm64-linux-75 工具链"
    echo "   预期路径: ${CROSS_COMPILE}gcc"
    exit 1
fi

echo "✅ 工具链检查通过: ${CROSS_COMPILE}gcc"
${CROSS_COMPILE}gcc --version | head -n 1

# 设置环境变量
export PATH="${TOOLCHAIN_BIN}:${PATH}"
export CC="${CROSS_COMPILE}gcc"
export CXX="${CROSS_COMPILE}g++"

# 目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RGA_SOURCE_DIR="${SCRIPT_DIR}/.tool/rga-linux-rga-multi"
BUILD_DIR="${RGA_SOURCE_DIR}/build_arm75"
INSTALL_DIR="${RGA_SOURCE_DIR}/install"
TARGET_LIB_DIR="${SCRIPT_DIR}/third_lib/rk3562/rkrga/lib"
TARGET_INC_DIR="${SCRIPT_DIR}/third_lib/rk3562/rkrga/include"

# 清理旧的编译目录
echo "🧹 清理旧的编译目录..."
rm -rf "${BUILD_DIR}"
rm -rf "${INSTALL_DIR}"
mkdir -p "${BUILD_DIR}"

cd "${BUILD_DIR}"

echo "🔧 配置 CMake..."
cmake "${RGA_SOURCE_DIR}" \
    -DCMAKE_BUILD_TARGET=cmake_linux \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"

echo "🔨 编译 RGA SDK..."
make -j$(nproc)

echo "📦 安装 RGA SDK..."
make install

echo "📁 复制库文件到 third_lib..."
mkdir -p "${TARGET_LIB_DIR}"
mkdir -p "${TARGET_INC_DIR}"
cp "${INSTALL_DIR}/lib/librga.so" "${TARGET_LIB_DIR}/"
cp "${INSTALL_DIR}/lib/librga.a" "${TARGET_LIB_DIR}/"
cp -r "${INSTALL_DIR}/include/"* "${TARGET_INC_DIR}/"

echo ""
echo "=========================================="
echo "✅ RGA SDK 编译完成！"
echo "   工具链: arm64-linux-75 (glibc 7.5)"
echo "   库文件: ${TARGET_LIB_DIR}/librga.so"
echo "   头文件: ${TARGET_INC_DIR}/"
echo "=========================================="
echo ""
echo "现在可以重新编译主程序了:"
echo "   cd ${SCRIPT_DIR} && make clean && make"
