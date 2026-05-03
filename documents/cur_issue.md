## 全项目「同类需要统一实现 / 拆分」的代码清单

我按**重复代码、待迁移代码、待抽象代码**三类整理，这是你后续优化的核心清单：

### 第一类：重复实现 → 必须统一复用 `common/queue`（最高优先级）

所有**手写队列 / 同步机制**的模块，全部替换为 `common/queue`：

1. ```
   src/link/frame_link/src/frame_link.c
   ```

   - 内部手写环形队列 + 互斥锁 + 条件变量 → 替换为 `common/queue`

   

2. ```
   src/bus/event_bus/src/event_bus.c
   ```

   - 事件总线内部队列 → 复用 `common/queue`

   

3. ```
   src/bus/data_bus/src/data_bus.c
   ```

   - 数据总线内部队列 → 复用 `common/queue`

   

4. 后续音频链路、AI 服务队列 → 统一使用 `common/queue`

------

### 第二类：通用抽象 → 必须迁移到 `common`（次优先级）

所有**通用功能、无业务绑定**的实现，从 `src` 移到 `common`：

1. 帧池 / 对象池抽象

   - 来源：`frame_link.c` 内部 `frame_node_t` 池管理
   - 目标：新增 `common/pool` 通用对象池组件

   

2. 总线实现

   - 来源：`src/bus/event_bus.c` / `data_bus.c`
   - 目标：`common/bus`

   

3. 状态机实现

   - 来源：`src/fsm/global_fsm.c` / `module_fsm.c`
   - 目标：`common/fsm`

   

4. 线程通用管理

   - 来源：各模块手写 `pthread`
   - 目标：`common/thread` 统一线程封装

   

------

### 第三类：`src` 瘦身 → 接口留 `src`，实现移出（架构规范）

`src` 下所有 `.c` 实现文件，**只留接口，移走实现**：

1. ```
   src/link/frame_link/src/frame_link.c
   ```

   - 接口：`src/link/frame_link/inc/frame_link.h`（保留）
   - 实现：移至 `plugins/service_plugins` 或 `common/link`

   

2. ```
   src/bus/*/src/*.c
   ```

   - 接口保留，实现移至 `common/bus`

   

3. ```
   src/fsm/*/src/*.c
   ```

   - 接口保留，实现移至 `common/fsm`

   

4. ```
   src/service/*/src/*.c
   ```

   - 接口保留，实现移至 `plugins/service_plugins`

   

------

### 第四类：易变业务 → 全部下沉到 `plugins`（你的核心需求）

所有**硬件适配、业务逻辑、可替换模块**，已在 `plugins`，保持并补充：

1. `plugins/hal_plugins`：所有硬件驱动（USB 视频、后续音频、摄像头）
2. `plugins/service_plugins`：所有服务实现（采集、AI、编码、推流）
3. `plugins/app_plugins`：所有应用业务逻辑（demo、业务 APP）
