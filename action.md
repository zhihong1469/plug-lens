# 瑞芯微 RK3562 组件使用手册

## 目录

- [一、组件概述](#一组件概述)
- [二、交叉编译产物](#二交叉编译产物)
- [三、项目集成配置](#三项目集成配置)
- [四、RKNN 模型转换](#四rknn-模型转换)
- [五、部署到目标板卡](#五部署到目标板卡)
- [六、API 使用示例](#六api-使用示例)

---

## 一、组件概述

| 组件 | 目录 | 描述 | 用途 |
|------|------|------|------|
| **RKMPP** | `third_lib/rk3562/rkmpp/` | 多媒体处理平台 | 视频编解码 (H.264/H.265)、图像处理 |
| **RKRGA** | `third_lib/rk3562/rkrga/` | 图形加速引擎 | 图像缩放、格式转换、旋转 |
| **RKNN** | `third_lib/rk3562/rknn/` | 神经网络推理 | AI 模型加速推理（NPU） |

---

## 二、交叉编译产物

### 2.1 目录结构

```
third_lib/rk3562/
├── rkmpp/                    # 多媒体处理平台
│   ├── include/rockchip/    # MPP 头文件 (25个)
│   └── lib/                 # 库文件
│       ├── librockchip_mpp.so      (3.0M)
│       ├── librockchip_mpp.a       (5.0M)
│       ├── librockchip_vpu.so      (83K)
│       └── pkgconfig/
│
├── rkrga/                   # 图形加速引擎
│   ├── include/             # RGA 头文件 (15个)
│   ├── lib/                 # 库文件
│   │   ├── librga.so        (263K)
│   │   └── librga.a         (465K)
│   └── bin/rgaImDemo        # 示例程序
│
└── rknn/                    # 神经网络推理
    ├── librknnrt.so         # RKNN 运行时 (5.4M)
    ├── librga.so            # RGA 依赖 (189K)
    ├── libmk_api.so         # 流媒体服务 (4.4M)
    ├── librockchip_mpp.so   # MPP 依赖
    ├── rknn_yolov5_demo     # 图像推理示例
    ├── rknn_yolov5_video_demo # 视频推理示例
    └── model/               # 模型文件
```

### 2.2 库文件详情

| 库名称 | 类型 | 大小 | 说明 |
|--------|------|------|------|
| `librockchip_mpp.so` | 动态库 | 3.0M | 多媒体编解码核心库 |
| `librockchip_vpu.so` | 动态库 | 83K | VPU 硬件编解码库 |
| `librockchip_mpp.a` | 静态库 | 5.0M | 静态链接版本 |
| `librga.so` | 动态库 | 263K | 图形加速核心库 |
| `librga.a` | 静态库 | 465K | 静态链接版本 |
| `librknnrt.so` | 动态库 | 5.4M | NPU 推理运行时 |

---

## 三、项目集成配置

### 3.1 头文件路径 (Makefile 第 79-81 行)

```makefile
# 瑞芯微库头文件
-I$(TOPDIR)/third_lib/rk3562/rkmpp/include
-I$(TOPDIR)/third_lib/rk3562/rkrga/include
-I$(TOPDIR)/.tool/rknn-toolkit2-master/rknpu2/runtime/Linux/librknn_api/include
```

### 3.2 库链接 (src/Makefile 第 45-50 行)

```makefile
# 瑞芯微库链接
-L$(TOPDIR)/third_lib/rk3562/rkmpp/lib \
	-lrockchip_mpp \
	-L$(TOPDIR)/third_lib/rk3562/rkrga/lib \
	-lrga \
	-L$(TOPDIR)/third_lib/rk3562/rknn \
	-lrknnrt
```

---

## 四、RKNN 模型转换

### 4.1 现有模型资源

你的项目已有 ONNX 模型：

```
.tool/Ultra-Light-Fast-Generic-Face-Detector-1MB-master/models/onnx/
├── version-RFB-320_simplified.onnx      # RFB 320x320 简化版
├── version-RFB-640.onnx                  # RFB 640x640
├── version-slim-320_simplified.onnx      # Slim 320x320 简化版
├── version-slim-320.onnx                 # Slim 320x320
└── ...
```

### 4.2 转换步骤

#### 步骤 1: 创建转换脚本

在项目目录下创建 `tools/convert_face_rknn.py`：

```python
#!/usr/bin/env python3
"""
UltraFace ONNX 模型转 RKNN 模型
目标平台: RK3562
"""
import os
import numpy as np
from rknn.api import RKNN

# 配置参数
PLATFORM = 'rk3562'           # 目标平台: rk3562/rk3566/rk3568/rk3588
MODEL_NAME = 'version-RFB-320_simplified'
INPUT_SIZE = 320               # 输入分辨率: 320 或 640
ONNX_MODEL = f'../.tool/Ultra-Light-Fast-Generic-Face-Detector-1MB-master/models/onnx/{MODEL_NAME}.onnx'
OUTPUT_DIR = './rknn_models'

def convert_to_rknn():
    """转换 ONNX 到 RKNN"""
    
    # 创建 RKNN 对象
    rknn = RKNN()
    
    # 配置转换参数
    # mean_values: 输入图像归一化的均值 (RGB格式)
    # std_values: 输入图像归一化的标准差
    rknn.config(
        mean_values=[[0, 0, 0]],      # 不做均值处理
        std_values=[[255, 255, 255]], # 归一化到 [0,1]
        target_platform=PLATFORM
    )
    
    # 加载 ONNX 模型
    print(f'--> Loading ONNX model: {ONNX_MODEL}')
    ret = rknn.load_onnx(ONNX_MODEL)
    if ret != 0:
        print('load onnx model failed!')
        return ret
    print('done')
    
    # 构建 RKNN 模型
    print('--> Building RKNN model...')
    ret = rknn.build(
        do_quantization=True,  # 启用量化，减小模型体积
        dataset='./dataset.txt'  # 量化校准数据集
    )
    if ret != 0:
        print('build rknn model failed.')
        return ret
    print('done')
    
    # 导出 RKNN 模型
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)
    
    rknn_model_path = f'{OUTPUT_DIR}/{MODEL_NAME}_{INPUT_SIZE}_{PLATFORM}.rknn'
    print(f'--> Exporting RKNN model: {rknn_model_path}')
    ret = rknn.export_rknn(rknn_model_path)
    if ret != 0:
        print('Export rknn model failed.')
        return ret
    print('done')
    
    # 释放资源
    rknn.release()
    print(f'\n转换完成！模型保存至: {rknn_model_path}')
    return 0

if __name__ == '__main__':
    convert_to_rknn()
```

#### 步骤 2: 创建量化校准数据集

创建 `tools/dataset.txt`，包含用于量化校准的图像路径列表：

```
./data/face1.jpg
./data/face2.jpg
./data/face3.jpg
./data/face4.jpg
./data/face5.jpg
```

> **提示**: 可以从 WiderFace 数据集中选取 5-10 张人脸图片

#### 步骤 3: 执行转换

```bash
# 1. 进入工具目录
cd tools

# 2. 确保 RKNN 环境已安装
pip install rknn-toolkit2

# 3. 运行转换脚本
python convert_face_rknn.py
```

#### 步骤 4: 验证模型

转换成功后会生成模型文件：

```
tools/rknn_models/version-RFB-320_simplified_320_rk3562.rknn
```

### 4.3 支持的模型格式

| 源格式 | 转换工具 | 支持程度 |
|--------|----------|----------|
| **ONNX** | rknn-toolkit2 | 完全支持 |
| TensorFlow Lite | rknn-toolkit2 | 完全支持 |
| Caffe | rknn-toolkit2 | 完全支持 |
| PyTorch (.pt) | 需先转 ONNX | 间接支持 |
| Darknet (.weights) | 需先转 ONNX | 间接支持 |

### 4.4 转换参数说明

```python
rknn.config(
    mean_values=[[123, 117, 104]],  # ImageNet 常用均值 (BGR格式)
    std_values=[[58.395, 57.12, 57.375]],  # ImageNet 标准差
    target_platform='rk3562'
)
```

**常用预训练模型配置**:

| 模型 | 输入尺寸 | mean_values | std_values |
|------|----------|-------------|------------|
| YOLOv5 | 640x640 | [[0, 0, 0]] | [[255, 255, 255]] |
| MobileNet | 224x224 | [[123.68, 116.78, 103.53]] | [[58.39, 57.12, 57.375]] |
| UltraFace | 320x320/640x640 | [[0, 0, 0]] | [[255, 255, 255]] |

---

## 五、部署到目标板卡

### 5.1 文件传输

```bash
# 库文件
scp -r third_lib/rk3562/rkmpp/lib/* root@192.168.x.x:/usr/local/lib/
scp -r third_lib/rk3562/rkrga/lib/* root@192.168.x.x:/usr/local/lib/
scp -r third_lib/rk3562/rknn/* root@192.168.x.x:/usr/local/lib/

# 头文件 (如需开发)
scp -r third_lib/rk3562/rkmpp/include/rockchip root@192.168.x.x:/usr/local/include/
scp -r third_lib/rk3562/rkrga/include/* root@192.168.x.x:/usr/local/include/
```

### 5.2 板卡环境配置

```bash
# 1. 更新动态库缓存
echo "/usr/local/lib" > /etc/ld.so.conf.d/rockchip.conf
ldconfig

# 2. 验证库文件
file /usr/local/lib/librknnrt.so
# 输出应为: ELF 64-bit LSB shared object, ARM aarch64

# 3. 检查依赖
ldd /usr/local/lib/librknnrt.so
```

### 5.3 运行时环境变量

```bash
# 方式1: 临时设置
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# 方式2: 永久设置 (写入 ~/.bashrc)
echo 'export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

---

## 六、API 使用示例

### 6.1 MPP 视频解码

```cpp
#include "rockchip/mpp_buffer.h"
#include "rockchip/mpp_frame.h"
#include "rockchip/mpp_decoder.h"
#include "rockchip/rk_mpi.h"

// 初始化 MPP 解码器
MppCtx ctx;
MppApi *mpi;
rk_s32 ret = mpp_create(&ctx, &mpi);
ret = mpp_init(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC, width, height);

// 解码一帧
MppPacket packet;
MppFrame frame;

// 设置输入数据
mpp_packet_set_data(packet, buffer, size);

// 输入压缩数据
mpi->decode_put_packet(ctx, packet);

// 获取解码后的图像
mpi->decode_get_frame(ctx, &frame);

// 获取图像信息
void* ptr = mpp_frame_get_buffer(frame);
int width = mpp_frame_get_width(frame);
int height = mpp_frame_get_height(frame);

// 释放资源
mpp_frame_destroy(frame);
mpp_packet_destroy(packet);
mpp_destroy(ctx);
```

### 6.2 RGA 图像处理

```cpp
#include "rga.h"
#include "im2d.h"

// 配置源图像和目标图像
rga_buffer_t src, dst;

src.fd = src_fd;           // 文件描述符
src.width = src_width;     // 宽度
src.height = src_height;   // 高度
src.format = RK_FORMAT_YCbCr_420_P;  // YUV420 格式
src.phys_addr = src_phys;  // 物理地址 (可选)

dst.fd = dst_fd;
dst.width = dst_width;
dst.height = dst_height;
dst.format = RK_FORMAT_RGB_888;  // RGB 格式

// 设置缩放参数
IM_NN_CTX nn_ctx;
imresize(&src, &dst, &nn_ctx);

// 调用 RGA 硬件加速
c_RgaBlit(&src, &dst, NULL, NULL, NULL, 0);
```

### 6.3 RKNN 模型推理

```cpp
#include "rknn_api.h"

rknn_context ctx;

// 1. 初始化 RKNN 上下文
const char* model_path = "/path/to/model.rknn";
ret = rknn_init(&ctx, model_path, 0, RKNN_FLAG_PRIOR_MEDIUM);
if (ret < 0) {
    printf("rknn_init failed!\n");
    return -1;
}

// 2. 查询模型输入输出信息
rknn_input_output_num io_num;
rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
printf("inputs: %d, outputs: %d\n", io_num.n_input, io_num.n_output);

// 3. 获取输入属性
rknn_tensor_attr input_attr;
input_attr.index = 0;
rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &input_attr, sizeof(input_attr));
printf("input size: %dx%d\n", input_attr.dims[2], input_attr.dims[3]);

// 4. 设置输入数据
rknn_input inputs[1];
inputs[0].index = 0;
inputs[0].buf = input_image_data;
inputs[0].size = input_size;
inputs[0].pass_through = 0;
inputs[0].fmt = RKNN_TENSOR_NHWC;  // 或 RKNN_TENSOR_NCHW
inputs[0].type = RKNN_TENSOR_UINT8;

rknn_inputs_set(ctx, 1, inputs);

// 5. 执行推理
ret = rknn_run(ctx, NULL);

// 6. 获取输出
rknn_output outputs[1];
outputs[0].index = 0;
outputs[0].want_float = 1;  // 获取浮点输出
rknn_outputs_get(ctx, 1, outputs, NULL);

// 7. 后处理 (目标检测等)
float* output = (float*)outputs[0].buf;
post_process(output, io_num.n_output);

// 8. 释放资源
rknn_outputs_release(ctx, 1, outputs);
rknn_destroy(ctx);
```

### 6.4 RGA + MPP + RKNN 完整流水线

```cpp
// 完整视频推理流程
void video_inference_pipeline(int video_fd) {
    // 1. MPP 解码获取帧
    MppFrame frame = decode_frame(video_fd);
    
    // 2. RGA 图像预处理 (缩放到模型输入尺寸)
    rknn_input rknn_input;
    preprocess_with_rga(frame, &rknn_input);
    
    // 3. RKNN 推理
    rknn_run(ctx, NULL);
    rknn_outputs_get(ctx, 1, outputs, NULL);
    
    // 4. 后处理 (NMS 等)
    std::vector<BoundingBox> boxes = post_process(outputs);
    
    // 5. RGA 绘制检测框
    draw_boxes_with_rga(frame, boxes);
    
    // 6. 释放资源
    mpp_frame_destroy(frame);
}
```

---

## 附录: 交叉编译环境变量

```bash
# 工具链路径
export CROSS_COMPILE=aarch64-none-linux-gnu-
export PATH=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin:$PATH

# 编译配置
export CC=aarch64-none-linux-gnu-gcc
export CXX=aarch64-none-linux-gnu-g++
export AR=aarch64-none-linux-gnu-ar
export LD=aarch64-none-linux-gnu-ld
```
