# 三个瑞芯微组件交叉编译分析报告

## 一、文件解压状态

已成功解压以下三个压缩包：

| 文件名 | 解压路径 | 描述 |
|--------|----------|------|
| `mpp-1.0.12.zip` | `.tool/mpp-1.0.12/` | 瑞芯微多媒体处理平台 (Media Process Platform) |
| `rga-linux-rga-multi.zip` | `.tool/rga-linux-rga-multi/` | 瑞芯微图形加速引擎 (Rockchip Graphics Accelerator) |
| `rknn-toolkit2-master.zip` | `.tool/rknn-toolkit2-master/` | 瑞芯微神经网络推理工具包 |

---

## 二、交叉编译产物目录结构

### 2.1 最终部署目录

```
third_lib/rk3562/
├── rkmpp/                    # 多媒体处理平台
│   ├── include/rockchip/    # MPP 头文件
│   └── lib/                 # MPP 动态/静态库
│       ├── librockchip_mpp.so
│       ├── librockchip_mpp.so.0
│       ├── librockchip_mpp.so.1
│       ├── librockchip_mpp.a
│       ├── librockchip_vpu.so
│       └── pkgconfig/
│
├── rkrga/                   # 图形加速引擎
│   ├── include/             # RGA 头文件
│   ├── lib/                 # RGA 动态/静态库
│   │   ├── librga.so
│   │   └── librga.a
│   └── bin/
│       └── rgaImDemo
│
└── rknn/                    # 神经网络推理
    ├── librknnrt.so         # RKNN 运行时库
    ├── librga.so            # RGA 依赖库
    ├── libmk_api.so         # ZLMediaKit API
    ├── librockchip_mpp.so   # MPP 依赖库
    ├── rknn_yolov5_demo     # 图像推理示例
    ├── rknn_yolov5_video_demo # 视频推理示例
    └── model/               # 模型文件
        ├── RK3562/
        │   └── yolov5s-640-640.rknn
        ├── bus.jpg
        └── coco_80_labels_list.txt
```

### 2.2 库文件详情

| 库名称 | 类型 | 大小 | 说明 |
|--------|------|------|------|
| **RKMPP** | | | |
| `librockchip_mpp.so` | 动态库 | 3.0M | 多媒体编解码核心库 |
| `librockchip_vpu.so` | 动态库 | 83K | VPU 硬件编解码库 |
| `librockchip_mpp.a` | 静态库 | 5.0M | 静态链接版本 |
| **RKRGA** | | | |
| `librga.so` | 动态库 | 263K | 图形加速核心库 |
| `librga.a` | 静态库 | 465K | 静态链接版本 |
| **RKNN** | | | |
| `librknnrt.so` | 动态库 | 5.4M | NPU 推理运行时 |
| `librga.so` | 动态库 | 189K | RGA 依赖 |
| `libmk_api.so` | 动态库 | 4.4M | 流媒体服务 |

---

## 三、项目集成配置

### 3.1 更新全局头文件路径

在项目根目录 `Makefile` 的 `GLOBAL_INC` 中添加：

```makefile
# 在 GLOBAL_INC 末尾添加瑞芯微库头文件路径
GLOBAL_INC := \
	...
	-I$(TOPDIR)/third_lib/rk3562/rkmpp/include \
	-I$(TOPDIR)/third_lib/rk3562/rkrga/include
```

### 3.2 在 plugins/Makefile 中添加库链接

如果插件需要使用 RKMPP 或 RKRGA：

```makefile
# plugins/Makefile

LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lcom \
	-L$(TOPDIR)/third_lib/rk3562/rkmpp/lib \
	-L$(TOPDIR)/third_lib/rk3562/rkrga/lib \
	-lrockchip_mpp \
	-lrga \
	-pthread
```

### 3.3 在 src/Makefile 中添加库链接

```makefile
# src/Makefile

LDFLAGS := \
	-L$(OUTPUTDIR) \
	-lplug \
	-lcom \
	...
	# 瑞芯微多媒体和图形加速库
	-L$(TOPDIR)/third_lib/rk3562/rkmpp/lib \
	-L$(TOPDIR)/third_lib/rk3562/rkrga/lib \
	-lrockchip_mpp \
	-lrga \
	-pthread
```

---

## 四、编译 MPP (Media Process Platform)

### 4.1 目录结构

```
mpp-1.0.12/
├── inc/           # 头文件目录
├── mpp/           # 核心MPP库源码
├── kmpp/          # Kernel MPP源码
├── osal/          # 操作系统抽象层
├── build/
│   └── linux/
│       ├── aarch64/      # ARM64交叉编译配置
│       ├── arm/          # ARM32交叉编译配置
│       └── opt_proc.sh   # 工具链处理脚本
└── test/          # 测试代码
```

### 4.2 交叉编译配置文件

**工具链配置**: `build/linux/aarch64/arm.linux.cross.cmake`

```cmake
SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_C_COMPILER "${TOOLCHAIN}gcc")
SET(CMAKE_CXX_COMPILER "${TOOLCHAIN}g++")
SET(CMAKE_SYSTEM_PROCESSOR "armv8-a")

add_definitions(-fPIC)
add_definitions(-DARMLINUX)
add_definitions(-Dlinux)
```

### 4.3 编译命令

```bash
cd .tool/mpp-1.0.12
rm -rf build_Linux && mkdir -p build_Linux && cd build_Linux

export CC=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc
export CXX=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++

cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_SYSTEM_NAME=Linux \
         -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
         -DCMAKE_C_COMPILER=$CC \
         -DCMAKE_CXX_COMPILER=$CXX \
         -DCMAKE_INSTALL_PREFIX=/home/luo/linux/6ull/project/plug-lens/third_lib/rk3562/rkmpp

make -j4 && make install
```

---

## 五、编译 RGA (Rockchip Graphics Accelerator)

### 5.1 目录结构

```
rga-linux-rga-multi/
├── core/          # 核心RGA实现
├── im2d_api/      # IM2D API实现
├── include/       # 头文件
├── samples/       # 示例代码
├── cmake/         # CMake工具模块
├── toolchains/    # 工具链配置
└── cross/         # Meson交叉编译配置
```

### 5.2 交叉编译配置文件

**CMake工具链**: `toolchains/toolchain_linux.cmake`

```cmake
SET(TOOLCHAIN_HOME "/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu")
SET(TOOLCHAIN_NAME "aarch64-none-linux-gnu")

SET(CMAKE_C_COMPILER ${TOOLCHAIN_HOME}/bin/${TOOLCHAIN_NAME}-gcc)
SET(CMAKE_CXX_COMPILER ${TOOLCHAIN_HOME}/bin/${TOOLCHAIN_NAME}-g++)
SET(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_HOME})

SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

### 5.3 编译命令

```bash
cd .tool/rga-linux-rga-multi
rm -rf build_Linux && mkdir -p build_Linux && cd build_Linux

export CC=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc
export CXX=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++

cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_SYSTEM_NAME=Linux \
         -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
         -DCMAKE_C_COMPILER=$CC \
         -DCMAKE_CXX_COMPILER=$CXX \
         -DCMAKE_BUILD_TARGET=cmake_linux \
         -DCMAKE_INSTALL_PREFIX=/home/luo/linux/6ull/project/plug-lens/third_lib/rk3562/rkrga

make -j4 && make install
```

---

## 六、编译 RKNN-Toolkit2 示例

### 6.1 目录结构

```
rknn-toolkit2-master/
├── rknn-toolkit2/      # Python工具包（PC端）
├── rknn_toolkit_lite2/ # 轻量化工具包
└── rknpu2/             # RKNN NPU运行时
    ├── runtime/         # 预编译库
    │   ├── Linux/
    │   │   └── librknn_api/
    │   │       ├── aarch64/
    │   │       └── armhf/
    │   └── Android/
    └── examples/       # 示例代码
        └── rknn_yolov5_demo/
```

### 6.2 预编译运行时库

**Linux平台**:
- `runtime/Linux/librknn_api/aarch64/librknnrt.so` - ARM64
- `runtime/Linux/librknn_api/armhf/librknnrt.so` - ARM32

**Android平台**:
- `runtime/Android/librknn_api/arm64-v8a/librknnrt.so`
- `runtime/Android/librknn_api/armeabi-v7a/librknnrt.so`

### 6.3 编译示例命令

```bash
cd .tool/rknn-toolkit2-master/rknpu2/examples/rknn_yolov5_demo
rm -rf build_Linux && mkdir -p build_Linux && cd build_Linux

export CC=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc
export CXX=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++

cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_SYSTEM_NAME=Linux \
         -DTARGET_SOC=RK3562 \
         -DCMAKE_INSTALL_PREFIX=/home/luo/linux/6ull/project/plug-lens/third_lib/rk3562/rknn

make -j4 && make install
```

### 6.4 支持的平台

| 脚本 | 平台 |
|------|------|
| `build-linux_RK3562.sh` | RK3562 |
| `build-linux_RK3566_RK3568.sh` | RK3566/RK3568 |
| `build-linux_RK3588.sh` | RK3588 |

---

## 七、关键配置对比

| 组件 | 构建系统 | 工具链指定方式 | 架构支持 |
|------|----------|---------------|----------|
| **MPP** | CMake | `CC/CXX` 环境变量 | ARM32/ARM64 |
| **RGA** | CMake/Meson | `CC/CXX` 环境变量 | ARM32/ARM64 |
| **RKNN** | CMake | `CC/CXX` 环境变量 | ARM32/ARM64 |

---

## 八、注意事项

1. **工具链版本**: 推荐使用 `gcc-arm-10.3-2021.07` 版本，与官方保持一致
2. **依赖关系**: RKNN示例依赖RGA和MPP库，需先编译这两个组件
3. **目标SOC**: 编译RKNN时需指定`-DTARGET_SOC`参数（RK3562/RK3566/RK3568/RK3588）
4. **运行时库**: RKNN提供预编译的`librknnrt.so`，无需源码编译

---

## 九、部署到目标板卡

### 9.1 文件传输

将编译产物复制到 RK3562 板卡：

```bash
# 通过 scp 传输
scp -r /home/luo/linux/6ull/project/plug-lens/third_lib/rk3562/* root@192.168.x.x:/usr/local/lib/

# 或者通过 NFS 挂载
```

### 9.2 板卡上配置环境变量

```bash
# 编辑 /etc/ld.so.conf.d/rockchip.conf
/usr/local/lib

# 更新动态库缓存
ldconfig

# 或者临时设置
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

### 9.3 验证库文件

```bash
# 查看库文件架构
file /usr/local/lib/librknnrt.so
# 输出应为: ELF 64-bit LSB shared object, ARM aarch64

# 检查依赖关系
ldd /usr/local/lib/librockchip_mpp.so
ldd /usr/local/lib/librga.so
ldd /usr/local/lib/librknnrt.so
```

---

## 十、API 使用示例

### 10.1 MPP 初始化和视频解码

```cpp
#include "rockchip/mpp_buffer.h"
#include "rockchip/mpp_frame.h"
#include "rockchip/mpp_decoder.h"
#include "rockchip/rk_mpi.h"

// 初始化 MPP
MppCtx ctx;
MppApi *mpi;
rk_s32 ret = mpp_create(&ctx, &mpi);
ret = mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC, width, height);

// 解码帧
MppPacket packet;
MppFrame frame;
mpi->decode_put_packet(ctx, packet);
mpi->decode_get_frame(ctx, &frame);

// 释放资源
mpp_frame_destroy(frame);
mpp_packet_destroy(packet);
mpp_destroy(ctx);
```

### 10.2 RGA 图像处理

```cpp
#include "rga.h"
#include "im2d.h"

RgaSURF_FORMAT src_fmt = RK_FORMAT_YCbCr_420_P;
RgaSURF_FORMAT dst_fmt = RK_FORMAT_RGB_888;

rga_buffer_t src, dst;
src.fd = src_fd;
src.width = src_width;
src.height = src_height;
src.format = src_fmt;

dst.fd = dst_fd;
dst.width = dst_width;
dst.height = dst_height;
dst.format = dst_fmt;

// 调用 RGA 进行图像缩放和格式转换
c_RgaBlit(&src, &dst, NULL, NULL, NULL, 0);
```

### 10.3 RKNN 推理

```cpp
#include "rknn_api.h"

rknn_context ctx;

// 初始化 RKNN 上下文
rknn_init(&ctx, model_path, 0, RKNN_FLAG_PRIOR_MEDIUM);

// 获取输入输出信息
rknn_input_output_num io_num;
rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));

// 设置输入
rknn_input inputs[1];
inputs[0].index = 0;
inputs[0].buf = input_data;
inputs[0].size = input_size;
inputs[0].pass_through = 0;
rknn_inputs_set(ctx, 1, inputs);

// 执行推理
rknn_run(ctx, NULL);

// 获取输出
rknn_output outputs[1];
outputs[0].want_float = 1;
rknn_outputs_get(ctx, 1, outputs, NULL);

// 后处理（目标检测等）
post_process(outputs[0].buf);

// 释放资源
rknn_outputs_release(ctx, 1, outputs);
rknn_destroy(ctx);
```

---

## 十一、编译产物位置总结

| 组件 | 源目录 | 编译目录 | 安装目录 |
|------|--------|----------|----------|
| **MPP** | `.tool/mpp-1.0.12/` | `build_Linux/` | `third_lib/rk3562/rkmpp/` |
| **RGA** | `.tool/rga-linux-rga-multi/` | `build_Linux/` | `third_lib/rk3562/rkrga/` |
| **RKNN** | `.tool/rknn-toolkit2-master/rknpu2/examples/rknn_yolov5_demo/` | `build_Linux/` | `third_lib/rk3562/rknn/` |

---

## 十二、交叉编译环境变量

```bash
# 工具链路径
export CROSS_COMPILE=aarch64-none-linux-gnu-
export PATH=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin:$PATH

# 编译配置
export CC=aarch64-none-linux-gnu-gcc
export CXX=aarch64-none-linux-gnu-g++
export AR=aarch64-none-linux-gnu-ar
export LD=aarch64-none-linux-gnu-ld

# 验证工具链
${CC} --version
```
