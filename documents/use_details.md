# Makefile骨架细节
## 单进程makefile
优势自动检测头文件变动，编译效率高
缺点顶层makefile请按需微调，如：
 - 必须明确指定编译顺序（不自动扫描，避免顺序混乱）
 - 顺序：common（公共库）→ plugins（插件）→ src（核心框架）
 - SUBDIRS := common plugins src
## gcc 链接是会把没及时用的库舍弃，需注意顺序
- example:
链接所有库：必须先libplug.a+再libcom.a+ pthread
LDFLAGS := \

	-L$(OUTPUTDIR) \
	-lplug \
	-lcom \
	-pthread



# 多线程代码新增须知
## 多线程层级总览
> 【主线程】
> ├── main()
> │   ├── demo_app_run() (select 循环)
> │   └── 所有状态机/总线 API 调用
> │
> 【子线程】
> └── [FrameLink] 采集线程 (pthread_create)
>  └── _frame_link_capture_thread()

## 各模块 pthread 调用详细
1. 帧链路 - FrameLink (frame_link.c)
> FrameLink 锁层级 (无嵌套，安全)：
> ├── pool_lock (最底层)
> └── queue_lock (最底层)

2. 业务服务 - CaptureSrv (capture_srv.c)
> CaptureSrv 锁层级：
> └── ctx->lock (最底层)
>     └── 内部调用：module_fsm_post_event()
>         └── [Module FSM 内部锁] (内部会获取自己的锁)

**3. 核心组件 - 事件总线 (Event Bus) (event_bus.c)**
> Event Bus 锁层级：
> └── ctx->rwlock (最底层)
>     └── 内部调用：用户回调函数 (_demo_app_on_event)
>         └── [用户回调内部可能调用其他 API]

4. 核心组件 - 数据总线 (Data Bus) (data_bus.c)		
>   Data Bus 锁层级 (关键！)：
>   ├── ctx->lock (顶层)
>   │   └── [保护 item 分配]
>   │
>   ├── ctx->rwlock (顶层)
>   │   └── [保护 latest_item_index]
>   │
>   └── item->ref_lock (底层)
>    └── [保护单个 item 的引用计数]

**5. 核心组件 - 模块状态机 (Module FSM) (module_fsm.c)**
> Module FSM 锁层级：
> └── ctx->lock
>     ├── 锁内调用：业务层 event_handler (如果有)
>     │   └── [业务层回调应尽量简单！！！]
>     │
>     └── 锁外调用：上层 state_cb (重要！在 unlock 之后调用)
>         └── global_fsm_on_module_state_change()
>             └── [可能获取 Global FSM 锁]

6. 核心组件 - 全局状态机 (Global FSM) (global_fsm.c)
>   Global FSM 锁层级 (已修复！)：
>   └── ctx->lock
>    ├── [先拷贝模块列表到临时数组]
>    |--解锁后再调用module_fsm_post_event用户回调!!!

7. 通用组件 - 日志 (Log) (log.c)

   | 锁变量             | 保护资源 | `lock()` 位置                               | `unlock()` 位置                             |
   | ------------------ | -------- | ------------------------------------------- | ------------------------------------------- |
   | `g_log_lock`互斥锁 | 标准输出 | `log_init()``log_deinit()``log_set_level()` | `log_init()``log_deinit()``log_set_level()` |

   通用组件 - 队列 (Queue) (queue.h/c)

   | 锁变量           | 保护资源   | `lock()` 位置                                                | `unlock()` 位置                                              |
   | ---------------- | ---------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
   | `q->mutex`互斥锁 | 队列缓冲区 | `Queue_Put()``Queue_Get()``Queue_Peek()``Queue_IsEmpty()``Queue_IsFull()``Queue_GetCount()``Queue_Clear()` | `Queue_Put()``Queue_Get()``Queue_Peek()``Queue_IsEmpty()``Queue_IsFull()``Queue_GetCount()``Queue_Clear()` |

## 通用线程封装使用场景分析

### 1. 直接替换场景（推荐立即使用）

|      现有代码位置       |  替换建议  |                收益                 |
| :---------------------: | :--------: | :---------------------------------: |
| `frame_link.c` 采集线程 | ✅ 推荐替换 | 统一线程管理，增加名称 / 优先级配置 |
|   `demo_app.c` 主循环   | ⚠️ 保持现状 |     主循环是主线程，不需要封装      |
|    后续新增业务线程     | ✅ 强制使用 | 所有新线程统一使用 `common/thread`  |

### 2. 具体使用示例

```
// 示例：在 FrameLink 中使用
#include "thread.h"

// 原采集线程入口函数保持不变
static void* _frame_link_capture_thread(void *arg) {
    // ... 原有代码 ...
}

// 修改 frame_link_start()
video_err_t frame_link_start(frame_link_handle_t handle) {
    // ... 原有代码 ...
    
    // 【替换】使用通用线程封装
    thread_attr_t attr;
    thread_attr_init(&attr);
    attr.name = "capture_thread";
    attr.priority = THREAD_PRIORITY_HIGH;
    attr.stack_size = 128 * 1024; // 128KB 栈
    
    thread_err_t terr = thread_create(&ctx->capture_thread_obj,
                                       &attr,
                                       _frame_link_capture_thread,
                                       ctx);
    if (terr != THREAD_OK) {
        // 错误处理
    }
    
    // ...
}

// 修改 frame_link_stop()
video_err_t frame_link_stop(frame_link_handle_t handle) {
    // ... 原有代码 ...
    
    // 【替换】使用通用线程封装等待
    thread_join(&ctx->capture_thread_obj, NULL);
    
    // ...
}
```

## 潜在风险点总结 & 后续开发规范

### ✅ 已解决的死锁风险
1. **Global FSM → Module FSM 调用**：已通过"拷贝临时数组 + 锁外调用"修复
2. **Module FSM → Global FSM 回调**：已通过"先解锁，后回调"修复

### ⚠️ 后续开发必须遵守的规范
#### 规范 1：锁的层级必须清晰
```
【允许】
├── 高层锁 → 低层锁
│   └── Global FSM Lock (无) → Module FSM Lock (通过临时数组规避)
│   └── Data Bus ctx->lock → Data Bus item->ref_lock
│
【禁止】
├── 低层锁 → 高层锁 (反向嵌套)
│   └── 例如：在 Module FSM 回调中尝试获取 Global FSM 锁
│
└── 循环嵌套
    └── 例如：A锁 → B锁 → A锁
```

#### 规范 2：在锁内只做最必要的操作
```c
// ✅ 推荐
pthread_mutex_lock(&lock);
// 1. 只做数据拷贝/状态读取
temp_value = ctx->value;
pthread_mutex_unlock(&lock);

// 2. 在锁外做耗时操作/调用其他 API
do_something(temp_value);
other_module_api();

// ❌ 禁止
pthread_mutex_lock(&lock);
// 🔴 危险：在锁内调用其他模块 API
other_module_api(); 
// 🔴 危险：在锁内做耗时操作
sleep(1);
pthread_mutex_unlock(&lock);
```

#### 规范 3：Event Bus 回调注意事项
```c
// Event Bus 回调 (_demo_app_on_event) 是在 Event Bus 的 rdlock 中调用的
void _demo_app_on_event(const event_t *event, void *user_data) {
    // ✅ 允许：简单的数据处理、日志打印
    LOG_I("Received event");
    
    // ✅ 允许：调用 Data Bus 获取数据
    data_bus_acquire_latest(...);
    
    // ⚠️ 谨慎：避免调用 event_bus_publish() (虽然理论安全)
    
    // ❌ 禁止：不要尝试调用 event_bus_subscribe/unsubscribe (会尝试获取 wrlock，导致死锁)
}
```

#### 规范 4：退出顺序规范
```c
// 退出顺序必须严格遵守：
// 1. 先停止业务层 (Demo App)
demo_app_deinit();

// 2. 再停止服务层 (Capture Srv)
capture_srv_destroy();

// 3. 最后销毁核心组件 (FSM, Bus)
global_fsm_deinit();
data_bus_deinit();
event_bus_deinit();
```

### 后续开发检查清单
在新增代码时，先问自己这 3 个问题：
1. **我在持有锁吗？** 如果是，不要调用其他模块的 API
2. **我在回调函数里吗？** 如果是，不要尝试获取可能导致反向嵌套的锁
3. **我遵循退出顺序了吗？** 先停业务，再停服务，最后销毁核心

