flowchart TD
    A[main 入口层] --> B[demo_app 应用交互层<br/>(主线程select循环)]
    B --> C[global_fsm 全局状态机<br/>(系统级总控)]
    C --> D[module_fsm 模块状态机<br/>(每个业务模块专属)]
    D --> E[capture_srv 业务服务层<br/>(采集业务逻辑)]
    E --> F[frame_link 链路管理层<br/>(线程/队列/帧池)]
    F --> G[video_hal 硬件抽象层<br/>(V4L2封装)]