# 结构介绍

```mermaid
flowchart TD
    %% 1. 定义样式
    classDef layer fill:#e1f5fe,stroke:#01579b,stroke-width:2px,color:#000;
    classDef bus fill:#fff9c4,stroke:#fbc02d,stroke-width:2px,color:#000,stroke-dasharray: 5 5;

    %% 2. 定义节点
    A["🚀 Main 系统入口层"]
    B["📱 App 应用交互层"]
    C["📨 Event Bus 事件总线"]
    Z["⚙️ 业务服务层"]
    D["💾 Data Bus 数据总线"]

    %% 3. 定义子图（逻辑分组）
    subgraph Control_Flow [控制流 / 事件流]
        direction TB
        A <--> B
        B <--> C
        C <--> Z
    end

    subgraph Data_Flow [数据流]
        direction TB
        Z <--> D
    end

    %% 4. 应用样式
    class A,B,Z layer;
    class C,D bus;
```
> [!NOTE]
> 这里的数据总线和事件总线都支持字符式注册多条.这也是我们为了消除全局变量的设计关键

**例如:**
```
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