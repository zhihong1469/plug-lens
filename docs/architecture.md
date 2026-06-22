# 项目架构文档

## 一、项目概述

本项目是一个基于嵌入式 Linux 的视觉 AI 应用框架，采用**事件总线**和**数据总线**的双总线架构设计，实现模块间的松耦合通信，从根本上消除全局变量的使用。

### 1.1 设计思想

| 设计原则 | 说明 |
|----------|------|
| **去全局变量** | 通过数据总线和事件总线实现模块间通信 |
| **分层架构** | 基建层 → 插件层 → 应用层 清晰分离 |
| **接口封闭** | 源文件对内隐藏，只开放头文件接口 |
| **插件化业务** | 业务逻辑以插件形式动态加载 |
| **上下层解耦** | 上层业务服务与底层硬件抽象完全隔离 |

---

## 二、系统架构图

### 2.1 分层架构总览

```mermaid
flowchart TD
    %% 样式定义
    classDef appLayer fill:#e3f2fd,stroke:#1976d2,stroke-width:2px,color:#000;
    classDef busLayer fill:#fff9c4,stroke:#fbc02d,stroke-width:2px,color:#000,stroke-dasharray: 5 5;
    classDef serviceLayer fill:#e8f5e9,stroke:#388e3c,stroke-width:2px,color:#000;
    classDef infraLayer fill:#fce4ec,stroke:#c2185b,stroke-width:2px,color:#000;
    classDef baseLayer fill:#e0e0e0,stroke:#424242,stroke-width:2px,color:#000;

    %% 应用层 (Application Layer)
    subgraph AppLayer [应用层]
        direction LR
        A1["main() 系统入口"]
        A2["App 应用交互"]
    end
    
    %% 总线层 (Bus Layer) - 核心解耦层
    subgraph BusLayer [总线层 - 解耦核心]
        direction LR
        B1["📨 Event Bus 事件总线"]
        B2["💾 Data Bus 数据总线"]
    end
    
    %% 业务服务层 (Service Layer)
    subgraph ServiceLayer [业务服务层 - plugins]
        direction LR
        S1["face_detect_srv 人脸检测"]
        S2["net_push_srv RTSP推流"]
        S3["camera_srv 摄像头采集"]
        S4["其他业务服务"]
    end
    
    %% 基建层 (Infrastructure Layer)
    subgraph InfraLayer [基建层 - src]
        direction LR
        I1["camera_base 摄像头抽象"]
        I2["ai_model_base AI模型抽象"]
        I3["img_proc_base 图像处理抽象"]
        I4["led_base LED控制抽象"]
    end
    
    %% 基础层 (Foundation Layer)
    subgraph BaseLayer [基础层]
        direction LR
        F1["common/ 公共组件"]
        F2["third_lib/ 第三方库"]
    end
    
    %% 连接关系：严格单向依赖
    AppLayer --> BusLayer
    BusLayer <--> ServiceLayer
    ServiceLayer --> InfraLayer
    InfraLayer --> BaseLayer
    
    %% 应用样式
    class AppLayer appLayer;
    class BusLayer busLayer;
    class ServiceLayer serviceLayer;
    class InfraLayer infraLayer;
    class BaseLayer baseLayer;
    
    %% 标注解耦关系

```

### 2.2 层次解耦关系

```mermaid
flowchart LR
    %% 定义节点
    subgraph L5 [应用层]
        direction TB
        A["应用入口 / 交互逻辑"]
    end
    
    subgraph L4 [总线层]
        direction TB
        E["事件总线<br/>(控制指令)"]
        D["数据总线<br/>(视频数据)"]
    end
    
    subgraph L3 [业务服务层]
        direction TB
        FD["人脸检测服务"]
        NP["网络推流服务"]
        CA["摄像头采集服务"]
    end
    
    subgraph L2 [设备抽象层]
        direction TB
        CB["摄像头抽象接口"]
        AB["AI模型抽象接口"]
        IB["图像处理抽象接口"]
    end
    
    subgraph L1 [设备/库层]
        direction TB
        HW["设备抽象"]
        LIB["第三方库"]
    end
    
    %% 连接关系
    L5 --> L4
    L4 <--> L3
    L3 --> L2
    L2 --> L1
    
    %% 箭头标注
    style L5 fill:#e3f2fd,stroke:#1976d2
    style L4 fill:#fff9c4,stroke:#fbc02d,stroke-dasharray: 5 5
    style L3 fill:#e8f5e9,stroke:#388e3c
    style L2 fill:#fce4ec,stroke:#c2185b
    style L1 fill:#e0e0e0,stroke:#424242
```

### 2.3 服务间通信架构

```mermaid
flowchart TD
    %% 数据流向
    subgraph Camera [摄像头采集]
        CAM["camera_srv"]
    end
    
    subgraph VideoBus [视频数据总线]
        VID["YUYV原始帧"]
    end
    
    subgraph FaceDetect [人脸检测]
        FD["face_detect_srv"]
        AI["MNN/RKNN模型"]
    end
    
    subgraph AIBus [AI结果总线]
        RES["人脸检测结果"]
    end
    
    subgraph NetPush [网络推流]
        NP["net_push_srv"]
        ENC["H.264编码器"]
    end
    
    subgraph EventBus [事件总线]
        EVT["控制事件"]
    end
    
    %% 数据流
    CAM -->|"生产"| VID
    VID -->|"消费"| FD
    FD -->|"生产"| RES
    RES -->|"消费"| NP
    VID -->|"消费"| ENC
    
    %% 事件流
    EVT <--> CAM
    EVT <--> FD
    EVT <--> NP
    
    %% 样式
    classDef producer fill:#c8e6c9,stroke:#388e3c;
    classDef consumer fill:#ffecb3,stroke:#ff9800;
    classDef bus fill:#fff9c4,stroke:#fbc02d,stroke-dasharray: 5 5;
    
    class CAM producer;
    class FD consumer;
    class FD producer;
    class NP consumer;
    class ENC consumer;
    class VID bus;
    class RES bus;
    class EVT bus;
```

---

## 三、目录结构

```
plug-lens/
├── src/                    # 基建层（几乎不需要修改）
│   ├── base/               # 基础组件
│   │   ├── camera/        # 摄像头抽象
│   │   ├── ai_model/      # AI 模型抽象
│   │   └── led/           # LED 控制
│   ├── bus/               # 总线实现
│   │   ├── event_bus/     # 事件总线
│   │   └── data_bus/      # 数据总线
│   └── app/               # 应用入口
│
├── plugins/                # 插件层（主要开发区域）
│   ├── face_detect/       # 人脸检测服务
│   ├── rtsp_stream/       # RTSP 推流服务
│   └── ...
│
├── common/                # 公共组件（重点关注头文件）
│   ├── log/               # 日志组件
│   ├── queue/             # 队列组件
│   ├── thread/            # 线程组件
│   ├── pool/              # 内存池
│   ├── img_proc/          # 图像处理
│   └── ...
│
├── third_lib/             # 第三方库（按开发板分类）
│   ├── rk3562/            # RK3562 板级支持
│   │   ├── rkmpp/         # 多媒体处理
│   │   ├── rkrga/         # 图形加速
│   │   └── rknn/          # 神经网络推理
│   ├── rk3568/            # RK3568 板级支持
│   └── aarch64/            # ARM64 通用库
│       ├── MNN/            # MNN 推理
│       ├── live555/        # RTSP
│       └── ...
│
├── scripts/               # 手动脚本
│
├── docs/                  # 文档目录
│
├── board/                 # 板级支持层（预留）
│
├── build/                 # 编译中间产物
│
├── output/                # 编译产物输出
│   └── vision_ai_app     # 最终可执行文件
│
├── .tool/                 # 第三方库源码（用于交叉编译）
│   ├── mpp-1.0.12/       # MPP 源码
│   ├── rga-linux-rga-multi/  # RGA 源码
│   └── rknn-toolkit2-master/ # RKNN 工具链
│
├── Makefile               # 顶层 Makefile
├── Makefile.build         # 编译规则
└── .vscode/              # VSCode 配置
```

---

## 四、目录详解

### 4.1 `src/` - 基建层

**作用**: 项目的基础框架层，提供了系统运行的核心组件。

**特点**: 
- 几乎不需要修改
- 源文件隔离，对外只开放头文件接口

**核心子目录**:

| 目录 | 说明 |
|------|------|
| `src/base/camera/` | 摄像头抽象接口 |
| `src/base/ai_model/` | AI 模型抽象接口 |
| `src/base/led/` | LED 控制接口 |
| `src/bus/event_bus/` | 事件总线实现 |
| `src/bus/data_bus/` | 数据总线实现 |
| `src/app/` | 应用入口 (main.c) |

**关键文件**:
- `src/app/src/main.c` - 程序入口点

---

### 4.2 `plugins/` - 插件层（主要开发区域）

**作用**: 业务逻辑和服务实现的核心区域。

**特点**: 
- 实际代码实现
- 需要经常修改
- 可插拔设计

**典型插件**:

| 插件 | 说明 |
|------|------|
| `face_detect/` | 人脸检测服务 |
| `rtsp_stream/` | RTSP 推流服务 |

---

### 4.3 `common/` - 公共组件

**作用**: 各模块都可能用到的通用组件库。

**特点**: 
- 头文件对外开放
- 源文件对内隐藏
- 可复用性高

**核心组件**:

| 组件 | 说明 |
|------|------|
| `log/` | 日志系统 |
| `queue/` | 队列数据结构 |
| `thread/` | 线程管理 |
| `pool/` | 内存池分配 |
| `img_proc/` | 图像处理工具 |
| `event_bus/` | 事件总线封装 |
| `data_bus/` | 数据总线封装 |
| `plugin/` | 插件管理 |
| `initcall/` | 自动初始化 |
| `mem_adapter/` | 内存适配 |
| `daemon/` | 守护进程 |
| `sd_mount/` | SD 卡挂载 |
| `network/` | 网络工具 |
| `sys_time/` | 系统时间 |

---

### 4.4 `third_lib/` - 第三方库

**作用**: 按开发板分类的交叉编译库。

**特点**:
- 已编译好的库文件
- 可直接复制到目标板卡使用
- 按 SOC 平台分类

**RK3562 组件**:

| 库 | 说明 | 用途 |
|----|------|------|
| `rkmpp/` | 多媒体处理平台 | 视频编解码 (H.264/H.265) |
| `rkrga/` | 图形加速引擎 | 图像缩放、格式转换 |
| `rknn/` | 神经网络推理 | AI 模型 NPU 加速 |

---

### 4.5 `scripts/` - 手动脚本

**作用**: 开发者手动编写的辅助脚本。

---

### 4.6 `build/` - 编译中间产物

**作用**: 编译过程中的中间文件（.o、.d 等）。

**特点**: 不需要关注，make clean 会清理。

---

### 4.7 `output/` - 编译产物输出

**作用**: 最终可执行文件和链接库的输出目录。

**关键文件**:
- `output/vision_ai_app` - 主程序可执行文件

---

### 4.8 `board/` - 板级支持层

**作用**: 不同板子的特殊支持代码（预留设计）。

**特点**: 对于应用开发暂时不需要特别关注。

---

### 4.9 `.tool/` - 第三方库源码

**作用**: 第三方库的源代码，用于交叉编译。

**特点**:
- 不与项目直接关联
- 编译后复制产物到 `third_lib/` 使用
- 包含: mpp、rga、rknn-toolkit2 等

---

### 4.10 `docs/` - 文档目录

**作用**: 项目文档存放处。

---

## 五、解耦架构设计

### 5.1 上下层解耦原则

本项目通过**三层解耦架构**实现上下层的完全隔离：

| 层次 | 名称 | 职责 | 依赖方向 |
|------|------|------|----------|
| L5 | 应用层 | 系统入口和顶层协调 | 只依赖总线层 |
| L4 | 总线层 | 事件和数据的中转分发 | 不依赖任何业务层 |
| L3 | 业务服务层 | 具体业务逻辑实现 | 依赖基建层，通过总线通信 |
| L2 | 基建层 | 硬件抽象接口定义 | 依赖公共组件和第三方库 |
| L1 | 基础层 | 公共组件和第三方库 | 无外部依赖 |

### 5.2 解耦点说明

**解耦点 1：应用层 ↔ 业务层**
- 应用层不直接调用业务服务接口
- 通过事件总线发送控制指令
- 通过数据总线获取业务数据
- **优势**：新增/替换业务服务无需修改应用层

**解耦点 2：业务服务之间**
- 服务之间无直接函数调用
- 通过数据总线传递数据（生产者-消费者模式）
- 通过事件总线协调状态
- **优势**：服务可独立开发、测试、部署

**解耦点 3：业务层 ↔ 基建层**
- 业务层只调用抽象接口（如 `camera_base`、`ai_model_base`）
- 具体硬件实现由基建层封装
- **优势**：同一业务逻辑可适配不同硬件平台

### 5.3 架构优势

| 优势 | 说明 |
|------|------|
| **可扩展性** | 新增业务服务只需实现接口并注册到总线 |
| **可维护性** | 模块独立，定位问题和修复更高效 |
| **可移植性** | 更换硬件平台只需替换基建层实现 |
| **可测试性** | 各模块可独立进行单元测试 |
| **低耦合** | 模块间依赖最小化，变更影响可控 |

---

## 六、总线设计

### 6.1 事件总线 (Event Bus)

**用途**: 系统控制命令和事件通知的传递。

**特点**: 支持字符串注册多条总线，实现模块间解耦。

**系统事件总线名称定义**:

```c
/** 系统事件总线名称 | 系统级控制事件通信总线 */
#define SYS_EVENT_BUS_NAME        "sys_event"
/** 系统数据总线名称 | 通用数据传输总线 */
#define SYS_DATA_BUS_NAME         "sys_data"
/** 视频数据总线名称 | 摄像头YUYV原始帧总线（采集服务生产） */
#define VIDEO_DATA_BUS_NAME       "video"
/** AI RGB数据总线名称 | AI模型输入RGB帧总线（人脸服务生产） */
#define AI_RGB_DATA_BUS_NAME      "ai_rgb"
/** 人脸结果数据总线名称 | 人脸检测结果输出总线 */
#define FACE_YUV_DATA_BUS_NAME    "face_result"
/** H264流数据总线名称 | RTSP推流H264码流传输总线 */
#define H264_RTSP_DATA_BUS_NAME   "h264_stream_bus"
```

### 6.2 数据总线 (Data Bus)

**用途**: 视频帧、AI 数据等大容量数据的传输。

**特点**: 支持多生产者多消费者模式。

---

## 七、编译说明

### 7.1 编译产物位置

| 产物类型 | 位置 |
|----------|------|
| 中间产物 | `build/` |
| 最终可执行文件 | `output/vision_ai_app` |
| 链接库 | `output/` |

### 7.2 编译命令

```bash
# 完整编译
make

# 清理
make clean
```

---

## 八、瑞芯微库集成

已在 Makefile 中添加 RK3562 组件支持：

### 8.1 头文件路径

```makefile
-I$(TOPDIR)/third_lib/rk3562/rkmpp/include
-I$(TOPDIR)/third_lib/rk3562/rkrga/include
-I$(TOPDIR)/.tool/rknn-toolkit2-master/rknpu2/runtime/Linux/librknn_api/include
```

### 8.2 库链接

```makefile
-L$(TOPDIR)/third_lib/rk3562/rkmpp/lib -lrockchip_mpp
-L$(TOPDIR)/third_lib/rk3562/rkrga/lib -lrga
-L$(TOPDIR)/third_lib/rk3562/rknn -lrknnrt
```
