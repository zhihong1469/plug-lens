# i.MX6ULL 裁剪版Buildroot 系统 | 守护进程+自动化运行 全上下文汇总
## 适用场景
基于 **100ask i.MX6ULL 开发板 + 裁剪版 Buildroot + BusyBox + sysvinit**（无 systemd、工具/命令/组件大量阉割），梳理**系统启动链路、Shell自动化脚本、C语言应用守护进程、配套监控/日志**全套逻辑、约束、历史踩坑、标准方案，作为后续开发/排错/脚本编写统一上下文。

---

# 一、基础固定环境上下文（所有配置&代码的前置约束）
## 1. 硬件&系统底座
1. **硬件**：100ask i.MX6ULL 开发板，USB 摄像头、LED 外设模块
2. **系统**：裁剪版 Buildroot，内核+BusyBox 极简环境
   - 初始化体系：`sysvinit`（传统 `/etc/inittab` 驱动，**无 systemd**）
   - 终端：`ttymxc0` 串口，BusyBox 内置 `getty`（**功能严重阉割**）
   - 命令集：部分 Linux 标准命令/参数不支持，需适配 BusyBox 语法
3. **全局目录结构（固定不变）**
```
/root/run_on_board/          # 程序根目录（守护进程工作目录）
├─ vision_ai_app             # 主业务可执行程序
├─ *.mnn                     # AI模型文件（相对路径依赖此目录）
├─ openh264/ libjpeg/ mnn/ libyuv/  # 第三方动态库
├─ drv/                      # LED 内核驱动 .ko 文件
└─ auto/                     # 所有自动化Shell脚本目录
   ├─ app_start.sh           # 总启动脚本
   ├─ app_stop.sh            # 停止脚本
   ├─ app_watchdog.sh        # Shell软件看门狗（进程保活）
   ├─ set_env.sh             # 动态库环境变量配置脚本
   ├─ log_rotate.sh          # 日志轮转脚本
   └─ cpu_monitor.sh         # CPU/资源监控脚本

/mnt/sdcard/                 # SD卡挂载点（日志/抓拍存储）
├─ log/                      # 应用日志、监控日志目录
└─ face_capture/             # 图片抓拍目录
```
4. **账号&登录**：原厂 `root`，**现阶段保留手动标签登录**，无自动进入终端

## 2. 裁剪系统核心限制（高频踩坑根源）
1. **BusyBox getty 阉割**：不支持 `--autologin` 等免登参数，强行修改会触发 `respawning too fast: disabled for 5 minutes` 终端禁用，**禁止修改登录相关配置**。
2. **sysinit 阶段环境残缺**：`/etc/inittab` 中 `sysinit` 类型任务（如 `rc.local`）执行时，**无用户自定义环境变量**（`LD_LIBRARY_PATH` 丢失）。
3. **USB设备时序延迟**：内核模块(LED)加载毫秒级完成；**USB摄像头枚举、/dev/video0 节点生成需要 2~5s**，短延时会导致设备打开失败。
4. **命令参数受限**：`top/du/df` 为 BusyBox 精简版，不支持完整 GNU 参数，日志/监控脚本必须适配。
5. **默认工作目录**：系统初始化、原生守护进程默认工作目录为 `/`，**相对路径会失效**（模型、库、配置文件找不到）。
6. **内核模块重复加载**：原厂部分驱动默认开机加载，重复 `insmod` 报 `File exists`，需屏蔽标准错误输出。

---

# 二、系统上电完整启动链路（核心逻辑，区分两大独立链路）
> 两条链路**完全解耦**：后台自动化任务 ≠ 串口登录终端，**不登录也可执行开机脚本**。
```
U-Boot 引导 → 加载内核/设备树 → 挂载根文件系统
        ↓
1. 系统初始化阶段（早于登录，核心自动化链路）
   执行 /etc/inittab 中所有 `::sysinit:` 条目
   → 执行自定义 rc.local（挂载SD卡、创建目录、设置时间、加载驱动）
   → 此阶段：USB设备仍在枚举，环境变量残缺
        ↓
2. 系统基础服务启动
   执行 /etc/init.d/rcS 系列开机服务
        ↓
3. 串口交互阶段（仅面向人工操作）
   启动 getty → 输出 login: 登录提示
   → 手动输入 root 登录 Shell 终端
   → 终端环境变量完整，手动执行脚本/程序无异常
```

### 关键结论
1. `rc.local` 属于**系统前置任务**，登录与否不影响其运行；
2. 摄像头、动态库依赖的环境、设备节点，**仅在登录后/延时足够时才能稳定就绪**；
3. 开发调试场景：**业务程序禁止开机自启**，仅保留基础系统动作自动执行。

---

# 三、系统层自动化配置规范（inittab + rc.local）
## 1. /etc/inittab 规范（原厂优先，最小修改）
### 原则
1. **完全保留原厂所有配置**，不修改 `getty` 登录行、关机行；
2. BusyBox 强制要求**每条规则必须有唯一ID**，禁止无ID条目；
3. 仅新增1行调用 `rc.local`，类型为 `sysinit`（前置执行）。

### 标准可用配置（最终稳定版）
```ini
# /etc/inittab 100ask i.MX6ULL 原厂基础 + 最小扩展
id:3:initdefault:

si0::sysinit:/bin/mount -t proc proc /proc
si1::sysinit:/bin/mount -o remount,rw /
si2::sysinit:/bin/mkdir -p /dev/pts /dev/shm
si3::sysinit:/bin/mount -a
si4::sysinit:/sbin/swapon -a
si5::sysinit:/bin/ln -sf /proc/self/fd /dev/fd 2>/dev/null
si6::sysinit:/bin/ln -sf /proc/self/fd/0 /dev/stdin 2>/dev/null
si7::sysinit:/bin/ln -sf /proc/self/fd/1 /dev/stdout 2>/dev/null
si8::sysinit:/bin/ln -sf /proc/self/fd/2 /dev/stderr 2>/dev/null
si9::sysinit:/bin/hostname -F /etc/hostname

# 【唯一新增】调用 rc.local 开机脚本（ID:rc01 合规，登录前执行）
rc01::sysinit:/etc/rc.local

rcS:12345:wait:/etc/init.d/rcS

# 原厂串口登录行：禁止修改/注释/加参数
mxc0::respawn:/sbin/getty -L ttymxc0 0 vt100

# Stuff to do for the 3-finger salute
#ca::ctrlaltdel:/sbin/reboot

# Stuff to do before rebooting
shd0:06:wait:/etc/init.d/rcK
shd1:06:wait:/sbin/swapoff -a
shd2:06:wait:/bin/umount -a -r

# The usual halt or reboot actions
hlt0:0:wait:/sbin/halt -dhp
reb0:6:wait:/sbin/reboot
```

## 2. /etc/rc.local 规范（分两种场景模板）
### 通用强制规则
1. 必须加执行权限：`chmod +x /etc/rc.local`；
2. 所有挂载、`insmod` 追加 `2>/dev/null`，屏蔽重复加载/挂载报错；
3. 不启动业务程序（调试模式），规避USB时序+环境变量问题；
4. 统一添加短延时，适配硬件初始化。

### 模板1：开发调试模式（当前主力使用，推荐）
仅执行**基础系统动作**，业务程序、看门狗、监控全部**手动启动**
```sh
#!/bin/sh
sleep 1

# 挂载SD卡，屏蔽报错
mount /dev/mmcblk0p1 /mnt/sdcard 2>/dev/null
# 创建日志/抓拍目录
mkdir -p /mnt/sdcard/log 2>/dev/null
mkdir -p /mnt/sdcard/face_capture 2>/dev/null
# 固定系统时间
date -s "2026-05-29 00:00:00"

# 加载LED驱动，屏蔽重复加载报错
insmod /root/run_on_board/drv/leddrv.ko 2>/dev/null
insmod /root/run_on_board/drv/chip_demo_gpio.ko 2>/dev/null
insmod /root/run_on_board/drv/board_A_led.ko 2>/dev/null

# 【注释】业务程序&看门狗 改为手动启动
# /root/run_on_board/auto/app_start.sh &

exit 0
```

### 模板2：量产全自动模式（项目定型后使用）
加长延时、导入库环境变量、开机自启全套服务
```sh
#!/bin/sh
# 导入动态库路径（弥补sysinit阶段环境缺失）
export APP_HOME=/root/run_on_board
export LD_LIBRARY_PATH=$APP_HOME/openh264:$APP_HOME/libjpeg:$APP_HOME/mnn:$APP_HOME/libyuv:$LD_LIBRARY_PATH

# 延长延时至5s，等待USB摄像头枚举完成
sleep 5

mount /dev/mmcblk0p1 /mnt/sdcard 2>/dev/null
mkdir -p /mnt/sdcard/log 2>/dev/null
mkdir -p /mnt/sdcard/face_capture 2>/dev/null
date -s "2026-05-29 00:00:00"

insmod /root/run_on_board/drv/leddrv.ko 2>/dev/null
insmod /root/run_on_board/drv/chip_demo_gpio.ko 2>/dev/null
insmod /root/run_on_board/drv/board_A_led.ko 2>/dev/null

# 开机自动启动全套业务+看门狗
/root/run_on_board/auto/app_start.sh &

exit 0
```

---

# 四、Shell层：自动化脚本&软件看门狗 逻辑规范
## 1. 脚本职责划分（解耦设计）
| 脚本 | 核心职责 | 运行要求 |
|------|----------|----------|
| `set_env.sh` | 配置 `LD_LIBRARY_PATH` 动态库路径 | **必须在启动程序前执行**，终端手动执行需用 `source/.` 加载 |
| `app_start.sh` | 总入口：切换工作目录、加载环境、后台拉起看门狗 | 统一切换到 `/root/run_on_board` 规避相对路径问题 |
| `app_watchdog.sh` | 软件看门狗：轮询检测 `vision_ai_app`，进程退出则自动重启 | 单例限制、日志记录、后台常驻 |
| `app_stop.sh` | 优雅停止：先停看门狗 → 再停业务程序，兜底强制杀死 | 按依赖顺序停止，避免僵尸进程 |
| `cpu_monitor.sh` | 采集CPU/内存/负载日志 | 后台并行运行，低采样频率，降低资源占用 |
| `log_rotate.sh` | 日志文件轮转，防止SD卡日志溢出 | 可手动/定时执行 |

## 2. 关键脚本核心约束
1. **`set_env.sh` 加载规则**
   - 错误：`./set_env.sh` → 子Shell执行，环境变量不生效；
   - 正确：`. ./set_env.sh` 或 `source ./set_env.sh` → 当前Shell生效。
2. **所有启动脚本必须先切换工作目录**
   统一执行 `cd /root/run_on_board`，解决守护进程默认目录 `/` 导致的**模型/相对路径找不到**问题。
3. **看门狗设计要点**
   - 增加PID文件做单例，防止脚本多开；
   - 检测间隔2~3s，重启延时3s，防雪崩重启；
   - 内部重新加载环境变量，保证子进程库依赖正常。

---

# 五、应用层：C语言守护进程（daemon.c）标准实现&避坑
## 1. 历史代码致命问题（已修复）
1. **调用顺序颠倒**：先开启日志守护模式 → 再创建守护进程，`fork` 会清空文件句柄，日志失效；
2. **缺少 `umask(0)`**：文件权限掩码默认值导致SD卡日志/文件写入权限不足；
3. **未设置工作目录**：默认目录 `/`，相对路径模型、配置加载失败；
4. **错误无日志输出**：裁剪系统无丰富报错，增加 `perror` 定位问题。

## 2. 标准适配版 daemon.c（i.MX6ULL 专用）
```c
/* SPDX-License-Identifier: MIT */
/**
 * @file    daemon.c
 * @brief   标准Linux守护进程实现（修复版，适配IMX6ULL）
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "daemon.h"

// 【固定】你的程序工作目录（必须和实际路径一致）
#define DAEMON_WORK_DIR    "/root/run_on_board"

int create_daemon(void)
{
    pid_t pid;

    // 1. 第一次fork：脱离终端，父进程退出
    pid = fork();
    if (pid < 0) {
        perror("fork1 failed");
        return -1;
    }
    if (pid > 0) {
        exit(0); // 父进程直接退出
    }

    // 2. 创建新会话，成为会话组长
    if (setsid() < 0) {
        perror("setsid failed");
        return -1;
    }

    // 3. 第二次fork：防止重新获取终端（标准规范）
    pid = fork();
    if (pid < 0) {
        perror("fork2 failed");
        return -1;
    }
    if (pid > 0) {
        exit(0);
    }

    // ===================== 【修复1】设置文件权限掩码 =====================
    umask(0);

    // ===================== 【修复2】设置工作目录（核心！） =====================
    if (chdir(DAEMON_WORK_DIR) < 0) {
        perror("chdir failed");
        return -1;
    }

    // 4. 重定向输入输出到 /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    return 0;
}
```

## 3. main函数调用顺序（强制规范）
```c
// 正确顺序：先创建守护进程 → 再关闭终端日志输出
#if RUN_PRODUCT_MODE
    LOG_I("Main: Creating daemon...");
    if (create_daemon() < 0) {
        LOG_E("Main: Failed to create daemon");
        return -1;
    }
    log_set_daemon_mode(1);  // 守护进程创建完成后，再切换日志模式
#endif
```

---

# 六、全量历史踩坑问题汇总（裁剪系统专属）
## 1. 登录&inittab 相关
1. 问题：`getty --autologin` 免登参数无效，触发 `respawning too fast 5分钟禁用`
   原因：BusyBox getty 阉割，不支持扩展参数
   方案：**放弃自动登录，保留原厂root手动登录**
2. 问题：`inittab` 无ID条目报错
   原因：BusyBox inittab 语法强制要求每条规则必须有ID
   方案：新增 `rc01::sysinit:/etc/rc.local` 合规条目

## 2. 开机自启&时序相关
1. 问题：`rc.local` 执行后LED驱动加载成功，但摄像头不工作
   原因：USB设备枚举延时(2~5s) > 脚本默认 `sleep 1`
   方案：量产模式延长 `sleep 5`；调试模式业务程序手动启动
2. 问题：手动运行程序正常，开机自启提示 `libxxx.so 找不到`
   原因：`sysinit` 阶段无自定义 `LD_LIBRARY_PATH`
   方案：`rc.local` 头部手动 `export` 库路径

## 3. 守护进程（C代码）相关
1. 问题：守护进程启动后，模型文件 `xxx.mnn open failed`
   原因：守护进程默认工作目录为 `/`，相对路径失效
   方案：`chdir(程序根目录)` 固定工作目录
2. 问题：守护进程日志无法写入SD卡
   原因：缺少 `umask(0)` 权限掩码 / 调用顺序颠倒
   方案：新增 `umask(0)`，先创建守护进程再切换日志模式

## 4. Shell脚本相关
1. 问题：`./set_env.sh` 执行后，库路径不生效
   原因：子Shell执行，环境变量无法回传给当前终端
   方案：使用 `. ./set_env.sh` / `source` 加载
2. 问题：`insmod` 重复加载驱动报 `File exists`
   原因：原厂驱动已默认加载
   方案：追加 `2>/dev/null` 屏蔽错误输出

## 5. 其他衍生问题
1. 问题：关机出现 `mxapp2/pulseaudio 进程不存在` 报错
   原因：原厂Qt桌面、音频服务未启动，关机尝试关闭
   方案：可删除 `/etc/init.d/S99mxapp2`、`S50pulseaudio` 彻底消除提示

---

# 七、分场景完整运行流程（落地操作）
## 场景1：开发调试模式（当前主力，最稳定）
### 配置状态
- `inittab`：仅新增 `rc01` 调用 `rc.local`
- `rc.local`：仅挂载SD、加载驱动、设时间，**注释业务启动代码**
- 登录：原厂 root/toor 手动登录

### 操作流程
1. 开发板上电 → 等待 `login:` 提示
2. 输入账号密码登录终端
3. 等待3s（USB摄像头枚举完成）
4. 手动加载环境 + 启动全套服务：
   ```sh
   . /root/run_on_board/auto/set_env.sh
   /root/run_on_board/auto/app_start.sh &
   # 按需启动CPU监控
   /root/run_on_board/auto/cpu_monitor.sh &
   ```
5. 停止服务：
   ```sh
   /root/run_on_board/auto/app_stop.sh
   killall cpu_monitor.sh
   ```

## 场景2：半自动化模式（开机基础任务+手动业务）
和场景1一致，仅优化别名简化操作：
```sh
# 配置别名
vi /etc/profile
alias start_app='. /root/run_on_board/auto/set_env.sh && /root/run_on_board/auto/app_start.sh &'
source /etc/profile
# 一键启动
start_app
```

## 场景3：量产全自动模式（无人值守）
1. 替换 `rc.local` 为【量产模板】（加长延时+导入库路径+启动业务）
2. 重启开发板，上电后**无需登录**，后台自动运行全套程序
3. 调试时可登录终端，通过 `ps`/`dmesg`/日志排查问题

---

# 八、日常排错常备命令集
## 1. 查看进程状态
```sh
ps | grep vision_ai_app
ps | grep app_watchdog.sh
ps | grep cpu_monitor.sh
```
## 2. 查看USB/摄像头硬件状态
```sh
ls /dev/video*
dmesg | grep -E "usb|video"
```
## 3. 查看动态库依赖
```sh
ldd /root/run_on_board/vision_ai_app
```
## 4. 查看日志
```sh
tail -f /mnt/sdcard/log/app.log
tail -f /mnt/sdcard/log/cpu_status.log
```
## 5. 快速解除终端5分钟禁用
```sh
init q
```

---
当前环境：100ask i.MX6ULL 开发板，裁剪版 Buildroot + BusyBox + sysvinit（无systemd）。
1. 系统限制：BusyBox getty功能阉割，不支持--autologin等免登参数，禁止修改登录相关配置；sysinit阶段环境变量残缺，USB摄像头枚举需要2-5秒延时。
2. 目录结构：程序根目录/root/run_on_board，脚本统一在auto目录，驱动在drv目录，动态库在对应子目录，SD卡挂载点/mnt/sdcard。
3. 架构要求：
   - 系统层：仅最小修改/etc/inittab，保留原厂登录，rc.local仅做基础挂载、驱动加载；
   - Shell脚本：启动前必须切换工作目录，环境变量用source加载，命令追加2>/dev/null屏蔽报错；
   - C守护进程：必须设置umask、固定工作目录，调用顺序：先创建守护进程，再关闭终端日志。
4. 当前使用场景：开发调试模式，优先手动启动业务程序，规避开机时序问题，追求系统稳定性。
5. 历史踩坑：相对路径失效、动态库路径丢失、USB时序不足、守护进程句柄丢失、inittab无ID报错、驱动重复加载报错。
请基于以上约束编写/修改代码、脚本或排错方案。
