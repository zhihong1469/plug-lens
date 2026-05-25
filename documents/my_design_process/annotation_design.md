# plug-eye 嵌入式Linux视觉AI采集终端 开源发布标准注释与优化规范
**全局统一配置（每次发布前仅需修改此处）**
```
PROJECT_NAME: plug-eye
GLOBAL_VERSION: v1.0.0
GLOBAL_RELEASE_DATE: 2026-05-29
AUTHOR_NAME: LuoZhihong
GITHUB_ID: zhihong1469
LICENSE: MIT License
```

---

## 整体要求
1. 所有注释严格遵循GitHub开源项目通用标准，清晰易懂，兼顾技术深度与可读性
2. 突出项目架构设计、核心技术亮点与工程化能力，适配秋招求职与技术比赛评审需求
3. 严格遵守MIT开源协议，明确版权归属与使用限制，保护个人知识产权
4. 注释风格统一，格式规范，无冗余内容，关键逻辑必须标注设计思路与注意事项
5. 采用「中英文双语精简注释」方案，英文使用简单词汇，兼顾可读性与开源专业性
6. 本名`LuoZhihong`用于正式版权声明、简历署名与学术场景；GitHub ID`zhihong1469`用于社区标识与项目链接

---

# 第一部分 标准注释规范
## 1. 文件头部注释（所有源文件必须包含）
```c
/**
 * @file    capture_service.c
 * @brief   USB Camera YUYV frame acquisition service
 *          USB摄像头YUYV原始帧采集服务
 * @details Publish frames to data bus, support multi-consumer zero-copy
 *          采集帧发布至数据总线，支持多消费者零拷贝访问
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-eye
 * @date    2026-05-29
 * @version v1.0.0
 *
 * @example
 * @code
 * // 初始化采集服务
 * capture_service_init();
 * // 启动采集
 * capture_service_start();
 * // 停止采集
 * capture_service_stop();
 * // 释放资源
 * capture_service_deinit();
 * @endcode
 *
 * @license MIT License
 * Copyright (c) 2026 LuoZhihong
 */
```

## 2. 头文件对外接口注释（重中之重）
所有对外公开的函数、结构体、枚举、宏定义必须添加完整注释，外部开发者仅通过头文件即可掌握接口用法

### 2.1 函数注释
```c
/**
 * @brief   Initialize camera capture service
 *          初始化摄像头采集服务
 * @param   width   Frame width (pixels) | 帧宽度(像素)
 * @param   height  Frame height (pixels) | 帧高度(像素)
 * @return  0 on success, negative error code on failure
 *          成功返回0，失败返回负错误码
 * @note    Must be called before any other capture functions
 *          必须在其他采集函数之前调用
 * @warning Only one instance allowed per process
 *          每个进程仅允许一个实例
 */
int capture_service_init(int width, int height);
```

### 2.2 结构体与枚举注释
```c
/**
 * @brief   Video frame structure
 *          视频帧结构体
 * @details Uses atomic reference count for memory management
 *          使用原子引用计数进行内存管理
 */
typedef struct {
    uint8_t *data;     /**< Frame data buffer | 帧数据缓冲区 */
    int width;         /**< Frame width | 帧宽度 */
    int height;        /**< Frame height | 帧高度 */
    int format;        /**< Pixel format | 像素格式 */
    int ref_count;     /**< Atomic reference count | 原子引用计数 */
    uint64_t timestamp; /**< Frame timestamp (us) | 帧时间戳(微秒) */
} video_frame_t;

/**
 * @brief   Capture service state
 *          采集服务状态
 */
typedef enum {
    CAPTURE_STOPPED,  /**< Service stopped | 服务已停止 */
    CAPTURE_RUNNING,  /**< Service running | 服务运行中 */
    CAPTURE_ERROR     /**< Service error | 服务错误 */
} capture_state_t;
```

### 2.3 宏定义注释
```c
/** Maximum number of frame buffers | 最大帧缓冲区数量 */
#define MAX_FRAME_BUFFERS 8

/** Default frame width | 默认帧宽度 */
#define DEFAULT_FRAME_WIDTH 640

/** Default frame height | 默认帧高度 */
#define DEFAULT_FRAME_HEIGHT 480
```

## 3. 源文件内部注释
### 3.1 核心逻辑注释
- 关键算法、架构设计、多线程同步、内存管理等核心代码必须添加注释
- 注释解释"为什么这么做"，而非"做了什么"
- 复杂逻辑分步骤说明，标注设计思路与优化点
```c
/* Dual-bus architecture design:
 * - Data bus: Transmit large video frames (zero-copy)
 * - Event bus: Transmit control commands and status notifications
 * This design decouples modules and improves system scalability
 *
 * 双总线架构设计：
 * - 数据总线：传输大数据视频帧（零拷贝）
 * - 事件总线：传输控制命令和状态通知
 * 该设计解耦模块，提高系统可扩展性
 */
```

### 3.2 优化点注释
所有性能优化、内存优化的代码必须标注优化原因与效果
```c
/* Optimization: Direct JPEG compression from YUYV format
 * Skip YUYV->RGB conversion, reduce CPU usage by ~30%
 * Tested on i.MX6ULL single-core platform
 *
 * 优化：从YUYV格式直接压缩JPEG
 * 跳过YUYV→RGB转换，CPU占用降低约30%
 * 在i.MX6ULL单核平台测试验证
 */
```

### 3.3 调试与问题注释
标注调试过程中解决的关键问题，展示问题排查能力
```c
/* Fixed: Memory leak caused by unreleased frames
 * Root cause: AI thread sometimes dropped frames without decrementing ref count
 * Solution: Add frame timeout auto-release mechanism
 *
 * 修复：未释放帧导致的内存泄漏
 * 根本原因：AI线程有时会丢弃帧但未减少引用计数
 * 解决方案：添加帧超时自动释放机制
 */
```

---

# 第二部分 README.md 标准模板
```markdown
# plug-eye 外设视觉AI采集终端
**正式版 v1.0.0 | 发布日期：2026-05-29**

基于NXP i.MX6ULL单核ARM平台开发的工业级边缘视觉AI系统，实现30fps实时RTSP视频推流、端侧离线人脸检测、触发式SD卡循环抓拍、守护进程无人值守运行。采用自研双总线解耦架构，针对低算力嵌入式设备深度优化，兼顾实时性与智能分析能力。

**作者：LuoZhihong**
**GitHub：https://github.com/zhihong1469/plug-eye**
**协议：MIT License**

## 技术栈
- 硬件平台：NXP i.MX6ULL Cortex-A7 @ 800MHz
- 操作系统：嵌入式Linux 5.4
- 视频采集：V4L2 USB摄像头驱动
- AI推理：MNN轻量级深度学习框架（INT8量化）
- 流媒体：Live555 RTSP服务器
- 图像编码：libjpeg-turbo 3.1.4.1
- 系统编程：POSIX多线程、互斥锁、条件变量、守护进程
- 自研架构：事件总线+数据总线双层通信架构

## 核心功能
- ✅ 30fps低延迟RTSP实时视频推流，支持多客户端同时访问
- ✅ 端侧离线人脸检测，160×120输入下推理耗时<100ms
- ✅ 人脸触发式SD卡循环存储，支持双介质容错
- ✅ 实时监控/离线历史回放双模式推流
- ✅ 守护进程异常保活 + Systemd开机自启
- ✅ 静态内存池+原子引用计数，杜绝内存泄漏与野指针

## 架构设计
### 分层架构
```
应用层 → 业务服务层（采集/AI/推流） → 双总线中间层 → 系统框架层
```

### 双总线通信
- **数据总线**：视频帧、AI结果等大数据零拷贝传输
- **事件总线**：控制指令、线程唤醒、状态通知

## 性能指标
| 指标 | 数值 | 说明 |
|------|------|------|
| 推流帧率 | 30fps | 640×480分辨率 |
| 端到端延迟 | <100ms | 从采集到客户端显示 |
| CPU占用 | <70% | 采集+推流+AI检测同时运行 |
| 内存占用 | <64MB | 包含所有运行时资源 |

## 部署说明
### 编译环境
- 交叉编译器：arm-linux-gnueabihf-gcc 9.3
- 依赖库：libjpeg-turbo、Live555、MNN

### 快速开始
```bash
# 克隆仓库
git clone https://github.com/zhihong1469/plug-eye.git
cd plug-eye

# 交叉编译
make CROSS_COMPILE=arm-linux-gnueabihf-

# 拷贝到开发板运行
scp plug-eye root@开发板IP:/root/
ssh root@开发板IP
./plug-eye
```

## 版权声明
本项目基于MIT协议开源，可自由用于个人学习、学术研究和商业项目。
- 保留原作者版权声明即可
- 作者拥有项目全部知识产权
- 开源不影响作者参加各类技术比赛和求职展示
```

---

# 第三部分 性能与内存优化指南
## 1. CPU性能优化
1. **图像运算优化**
   - 所有图像缩放使用最近邻插值算法
   - JPEG压缩质量设置为75，平衡画质与速度
   - 全程优先使用YUYV格式，避免不必要的格式转换

2. **AI推理优化**
   - 保持160×120输入分辨率
   - 开启MNN INT8量化，降低计算量
   - 设置MNN线程数为1，适配单核平台

3. **线程调度优化**
   - 优先级分级：采集线程 > 推流线程 > AI检测线程
   - 使用条件变量阻塞等待，禁止死循环轮询
   - 关闭所有调试打印，仅保留异常报错

## 2. 内存优化
1. **静态内存池**
   - 所有大块内存启动时预分配
   - 运行时禁止动态malloc/free
   - 帧缓存数量设置为8，平衡实时性与内存占用

2. **引用计数管理**
   - 严格遵循引用计数规则
   - 添加帧超时自动释放机制
   - 避免重复引用与重复释放

## 3. 稳定性优化
- 所有系统调用返回值必须检查
- 网络断连、摄像头断开后自动重连
- 增加内存和CPU使用监控
- 连续72小时压力测试验证

---

# 第四部分 开源发布检查清单
- [ ] 所有文件头部注释已更新为最新版本和日期
- [ ] 所有对外接口已添加完整注释
- [ ] 核心逻辑和优化点已标注说明
- [ ] 已删除所有调试代码和注释掉的代码
- [ ] 已移除所有个人敏感信息、硬编码IP和密码
- [ ] 已清理所有绝对路径，使用相对路径
- [ ] README.md已更新，包含完整的编译和运行说明
- [ ] LICENSE文件已添加（标准MIT协议）
- [ ] .gitignore文件已配置，过滤所有无用文件
- [ ] 本地编译测试通过，无警告和错误
- [ ] 已打版本标签`git tag v1.0.0`
- [ ] 已推送到GitHub和Gitee双平台

