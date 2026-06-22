# RK3562 视觉AI采集终端 开发&运行使用手册

**文档用途**：个人内部使用手册，整合交叉编译、NFS文件传输、双网络模式配置、硬件加速环境校验、脚本启停、RTSP拉流、调试排错全流程

**运行环境**：RK3562开发板(YM3562) + Ubuntu 22.04 LTS + sysvinit，架构为 aarch64

**工程目录**：板端主目录 `/root/run_on_board_rk3562`，NFS共享目录 `~/nfs/run_on_board_rk3562`

**交叉编译工具链**：支持两种工具链，请根据实际需求选择：
- `arm64-linux-103`（推荐）：RK3562 官方推荐，musl/none 10.3
- `arm64-linux-75`：Linaro 7.5 glibc，通用 ARM64

---

## 一、整体流程总览

1. **PC端**：配置 aarch64 交叉编译工具链 → 指定 RK3562 平台编译工程 → 拷贝可执行文件、AI模型、硬件加速库及所有第三方依赖至NFS共享目录
2. **开发板端**：二选一配置网络（以太网直连PC / 接入局域网路由器）→ 挂载NFS → 同步文件至板端固定目录
3. **板端前置操作**：校验硬件加速节点权限、系统时间兜底设置、SD卡挂载（程序内部已适配，外部为可选兜底操作）
4. **程序运行**：区分**手动启停脚本（调试）**、**开机自启（量产）**两种模式
5. **上层拉流**：根据开发板IP网段，使用 ffplay 拉取RTSP视频流播放
6. **调试排错**：远程GDBServer调试、防火墙处理、日志查看、硬件加速状态校验

---

## 二、PC端操作（编译 + 文件传输）

### 2.1 交叉编译工具链配置

本工程基于 aarch64 架构交叉编译工具链，支持两种工具链，对应 RK3562 两种运行模式：

```bash
# ============ 方式一：RK3562 官方推荐工具链（硬件模式）============
# GCC 10.3 musl/none，适合 RK3562 专用场景
# 用于硬件加速模式（启用 RGA 图像加速 + MPP 视频编码 + RKNN NPU）
use_toolchain arm64-linux-103
# 编译器：aarch64-none-linux-gnu-gcc
# 路径：/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/

# ============ 方式二：Linaro 通用工具链（软件模式）============
# GCC 7.5 glibc，适合通用 ARM64 或需要 glibc 的场景
# 用于软件模式（使用 libyuv + openh264 + MNN CPU 推理）
use_toolchain arm64-linux-75
# 编译器：aarch64-linux-gnu-gcc
# 路径：/usr/local/arm/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin/
```

### 2.2 工程编译

进入项目根目录，指定 RK3562 平台执行清理+编译。支持两种运行模式：

```bash
# ============================================================
# 模式一：软件模式（推荐，稳定性优先）
# 特点：使用 libyuv 图像处理 + openh264 软件编码 + MNN CPU 推理
# 适用：通用场景、追求稳定性、硬件库缺失时
# ============================================================
use_toolchain arm64-linux-75
make clean && make TARGET_PLATFORM=rk3562 ENGINE=software

# ============================================================
# 模式二：硬件模式（性能优先）
# 特点：使用 RGA 图像加速 + MPP 视频编码 + RKNN NPU 推理
# 适用：追求最佳性能、硬件库完整时
# 注意：需要确保 /dev/rga、/dev/mpp_service、/dev/rknpu 节点存在
# ============================================================
use_toolchain arm64-linux-103
make clean && make TARGET_PLATFORM=rk3562 ENGINE=hardware

# ============================================================
# 默认编译（软件模式，等同于上面的 software）
# ============================================================
make clean && make
```

> **模式选择建议**：
> - 优先使用**软件模式**，稳定性更好，依赖更少
> - 硬件模式需要完整的 RGA/MPP/RKNN 库和设备节点，缺失会导致启动失败

> 工程构建已内置头文件与依赖库检查逻辑，正常直接 make 即可；若出现依赖缺失可先检查第三方库路径配置。

### 2.3 依赖库检查

编译完成后，检查可执行文件依赖的动态库，提前确认库完整性：

```bash
# 软件模式（使用 arm64-linux-75 编译）
aarch64-linux-gnu-readelf -d output/vision_ai_app | grep NEEDED

# 硬件模式（使用 arm64-linux-103 编译）
aarch64-none-linux-gnu-readelf -d output/vision_ai_app | grep NEEDED
```

> 请确保所有动态库均已完成 aarch64 架构交叉编译，与工具链版本匹配。

### 2.4 批量拷贝文件至NFS共享目录

根据编译时选择的**运行模式**，拷贝对应的依赖库至 NFS 共享目录：

```bash
# 创建目录结构
mkdir -p ~/nfs/run_on_board_rk3562/{libjpeg,drv,openh264,libyuv,rknn,rkmpp,rga,mnn,auto_rk}

# 拷贝主程序
cp output/vision_ai_app ~/nfs/run_on_board_rk3562/

# 拷贝启动脚本
cp scripts/auto_rk/*.sh ~/nfs/run_on_board_rk3562/auto_rk/
chmod +x ~/nfs/run_on_board_rk3562/auto_rk/*.sh

# 拷贝AI模型文件
cp third_lib/face_detector/model_mnn/version-RFB/RFB-320-quant-KL-5792.mnn ~/nfs/run_on_board_rk3562/

# ============================================================
# 根据运行模式拷贝对应库文件
# ============================================================

# -------- 软件模式依赖库 --------
# 软件模式使用 lib_aarch64 目录下的库
# 注意：软件模式下不需要 rga/rkmpp/rknn 硬件库

# libjpeg-turbo 图像处理库
cp -a third_lib/libjpeg_turbo/lib_aarch64/*.so* ~/nfs/run_on_board_rk3562/libjpeg/


# openh264 软件编码库
cp -a third_lib/openh264/lib_aarch64/*.so* ~/nfs/run_on_board_rk3562/openh264/

# MNN 推理库（CPU 模式）
cp -a third_lib/mnn/lib_aarch64/*.so* ~/nfs/run_on_board_rk3562/mnn/

# libyuv 图像缩放库
cp -a third_lib/libyuv/lib_aarch64/*.so* ~/nfs/run_on_board_rk3562/libyuv/
# -------- 硬件模式依赖库 --------
# 硬件模式使用 lib_rk3562 目录下的库
# 需要确保板端存在对应硬件节点

# RGA 图像加速库
cp -a third_lib/rkrga/lib_rk3562/*.so* ~/nfs/run_on_board_rk3562/rga/

# MPP 视频编解码库
cp -a third_lib/rkmpp/lib_rk3562/*.so* ~/nfs/run_on_board_rk3562/rkmpp/

# RKNN NPU库
cp -a third_lib/rknn/lib_rk3562/*.so* ~/nfs/run_on_board_rk3562/rknn/

# libjpeg-turbo（硬件模式也需保留）
cp -a third_lib/libjpeg_turbo/lib_rk3562/*.so* ~/nfs/run_on_board_rk3562/libjpeg/

# MNN 推理库（RKNN 不可用时备用）
cp -a third_lib/mnn/lib_rk3562/*.so* ~/nfs/run_on_board_rk3562/mnn/
```

---

## 三、开发板网络配置 & NFS挂载（两种网段模式）

> 核心区分：两种网络模式对应不同IP网段、NFS挂载地址、RTSP拉流地址，根据使用场景二选一
>
> 板端固定工作目录：`/root/run_on_board_rk3562`（脚本、守护进程已适配此路径，**禁止修改**）
> NFS挂载参数统一：`nolock,port=2050`，适配 Buildroot 裁剪系统

### 3.1 模式一：以太网直连PC（纯开发调试---主力）

适用场景：仅本机PC和开发板联调，无其他设备接入

1. **网段规划**：PC网口 & 开发板eth0 统一网段 `192.168.5.x`
   - PC IP：`192.168.5.10`
   - 开发板IP：`192.168.5.11`

2. **开发板执行NFS挂载与文件同步**：

```bash
# 挂载PC端NFS共享目录至板端 /mnt
mount_nfs_wired

# 将NFS内完整项目文件同步至板端固定目录
mkdir -p /root/run_on_board_rk3562
cp -rp /mnt/run_on_board_rk3562/* /root/run_on_board_rk3562/

# 给自动化脚本赋予执行权限（首次部署必执行）
chmod +x /root/run_on_board_rk3562/auto_rk/*.sh
```

### 3.2 模式二：接入路由器局域网（多设备拉流播放---临时）

适用场景：开发板、PC、手机等多设备接入同一WiFi/路由器，任意设备均可拉取RTSP流

1. **网段规划**：所有设备统一路由器网段 `10.168.1.x`
   - PC NFS服务IP：`10.168.1.173`
   - 开发板IP：路由器自动分配或手动设置同网段IP

2. **开发板执行网络配置与NFS挂载**：

```bash
# 1. 临时设置静态IP（与路由器同网段，IP不冲突即可）
ifconfig eth0 10.168.1.9 netmask 255.255.255.0

# 2. 临时设置网关（与路由器网关一致）
route add default gw 10.168.1.1 eth0

# 3. 临时配置DNS
echo "nameserver 10.168.1.1" > /etc/resolv.conf
echo "nameserver 223.5.5.5" >> /etc/resolv.conf

# 可选连通性验证
ping 10.168.1.1   # 先ping网关，确认局域网连通
ping 223.5.5.5    # 再ping公网DNS，确认外网连通

# 4. 挂载PC端NFS共享目录
mount -t nfs -o nolock,port=2050 10.168.1.173:/home/luo/nfs /mnt

# 5. 同步文件至板端固定目录
mkdir -p /root/run_on_board_rk3562
cp -rp /mnt/run_on_board_rk3562/* /root/run_on_board_rk3562/
chmod +x /root/run_on_board_rk3562/auto_rk/*.sh
```

> **永久静态IP配置**（需长期固定网段时使用）：
> 编辑 `/etc/network/interfaces`，替换eth0配置：
> ```bash
> auto lo
> iface lo inet loopback
>
> # 以太网eth0 静态IP 永久配置
> auto eth0
> iface eth0 inet static
>     address 10.168.1.9
>     netmask 255.255.255.0
>     gateway 10.168.1.1
>     dns-nameservers 223.5.5.5 114.114.114.114
> ```

---

## 四、开发板前置初始化操作（通用流程）

> 说明：程序内部已实现SD卡自动挂载、目录创建，外部手动操作为兜底方案；硬件加速节点为RK3562必校验项。

### 4.1 系统时间兜底设置

两种网络模式下均建议手动校准时间，避免日志、抓拍文件时间错乱：

```bash
date -s "2026-06-21 10:00:00"
```

### 4.2 手动SD卡挂载（可选兜底）

程序内部已适配SD卡自动挂载逻辑，外部手动执行仅作兜底：

```bash
# 创建抓拍存储目录（兜底）
mkdir -p /mnt/sdcard/face_capture

# 手动挂载SD卡
mount /dev/mmcblk0p1 /mnt/sdcard

# 重要：拔卡/断电前务必卸载，防止文件系统损坏
umount /mnt/sdcard
```

### 4.3 硬件加速环境校验（RK3562必做）

确认RGA、MPP、VPU三大硬件加速节点正常，设置访问权限：

```bash
# 1. 校验硬件节点是否存在（已验证可用的设备）
ls -la /dev/rga              # RGA图像加速 ✅
ls -la /dev/mpp_service      # MPP视频编解码服务 ✅
ls -la /dev/video-enc0       # VPU硬件编码 ✅
ls -la /dev/mali0            # Mali GPU ✅

# 2. 赋予节点访问权限（程序非root运行时必须配置）
chmod 666 /dev/rga /dev/mpp_service /dev/video-enc0 /dev/mali0

# 3. 确认驱动已加载（可选）
lsmod | grep rga
lsmod | grep mpp
```

### ⚠️ NPU设备节点特殊说明

**问题**：`/dev/rknpu` 设备节点不存在，AI模型无法使用RKNN NPU推理

- 内核已配置 `CONFIG_ROCKCHIP_RKNPU=y`，但驱动未创建设备节点
- 可能原因：设备树中NPU节点被禁用或硬件引脚被复用
- **临时方案**：使用 MNN 软件推理（CPU）替代 RKNN NPU

### 4.4 LED驱动加载测试

RK3562 使用系统GPIO控制LED，无需额外驱动：

```bash
# RK3562 GPIO 已内置于系统
echo "✅ RK3562 GPIO 已就绪"
```

### 4.5 环境变量说明

整套脚本已内置环境变量加载逻辑（`set_env.sh`），自动配置硬件加速库、第三方库的动态链接路径：

```bash
export APP_HOME="/root/run_on_board_rk3562"
export LD_LIBRARY_PATH=$APP_HOME/rga:$APP_HOME/rkmpp:$APP_HOME/rknn:$APP_HOME/libjpeg:$APP_HOME/mnn:$LD_LIBRARY_PATH
```
```bash
export APP_HOME="/mnt/nfs/run_on_board_rk3562"
export LD_LIBRARY_PATH=$APP_HOME/rga:$APP_HOME/openh264:$APP_HOME/libjpeg:$APP_HOME/mnn:$APP_HOME/libyuv:$LD_LIBRARY_PATH
```
---

## 五、程序启停方案（两套方案，区分场景）

### 5.1 方案一：手动脚本启停（日常调试，主力使用）

基于自动化脚本，一键启动/停止全套服务（业务程序+软件看门狗）：

```bash
# 进入板端根目录
cd /root/run_on_board_rk3562

# 一键启动：自动切换目录、加载环境、拉起看门狗+业务程序
./auto_rk/app_start.sh &

# 一键停止：优雅关闭看门狗、业务进程，防止僵尸进程
./auto_rk/app_stop.sh

# 实时查看运行日志
tail -f /mnt/nfs/test/log/app.log
```

### 5.2 方案二：开机全自动自启（量产/无人值守）

依托系统 `sysvinit + rc.local` 实现上电自启，无需登录终端：

1. 编辑 `/etc/rc.local`，添加启动流程（内置延时、驱动加载、环境变量、启动脚本调用）
2. 确认 `/etc/inittab` 配置不变，保留sysinit阶段调用rc.local
3. 重启开发板，上电后系统自动完成：挂载SD卡 → 校验硬件节点 → 启动全套服务

---

## 六、PC端 RTSP 拉流播放（分两种网络模式）

使用 `ffplay` 拉取开发板RTSP视频流，IP地址跟随开发板网段切换，支持普通模式与低延迟模式

### 6.1 模式一：以太网直连PC（网段 192.168.5.x）

开发板IP：`192.168.5.11`

```bash
# 常规拉流播放
ffplay rtsp://192.168.5.11:8554/stream

# 低延迟播放（UDP传输，优先音视频同步，推荐实时预览）
ffplay -rtsp_transport udp -sync video -flags low_delay rtsp://192.168.5.11:8554/stream
```

### 6.2 模式二：路由器局域网（网段 10.168.1.x）

将命令中IP替换为**开发板当前局域网IP**，手机、平板等同网段设备均可使用此地址拉流：

```bash
# 替换为开发板实际IP，示例如下
ffplay rtsp://10.168.1.XXX:8554/stream
ffplay -rtsp_transport udp -sync video -flags low_delay rtsp://10.168.1.XXX:8554/stream
```

---

## 七、调试方案汇总

### 7.1 远程GDB调试（核心调试方案）

适用于守护进程、后台程序、复杂问题定位，使用 `gdbserver` 远程联动：

1. **开发板端**：启动gdbserver，绑定端口并拉起程序

```bash
# 端口 12345，单次连接模式
cd /root/run_on_board_rk3562
gdbserver --once :12345 ./vision_ai_app
```

2. **PC端**：连接远程gdbserver

```bash
# 进入PC端项目编译目录，启动 aarch64 交叉GDB

# RK3562 官方工具链
aarch64-none-linux-gnu-gdb output/vision_ai_app

# Linaro 工具链
aarch64-linux-gnu-gdb output/vision_ai_app

# 连接开发板（IP根据网络模式切换）
# 直连模式
target remote 192.168.5.11:12345
# 路由器局域网模式
target remote 10.168.1.XXX:12345

# 常用调试命令
thread apply all bt  # 查看所有线程调用栈（排查崩溃首选）
```

### 7.2 Windows 防火墙处理（拉流/远程调试失败优先排查）

Windows防火墙会拦截/限速RTSP流、GDB远程端口，调试前关闭防火墙：

> 以**管理员身份**打开CMD执行：
```cmd
netsh advfirewall set allprofiles state off
```

### 7.3 GDB配置脚本（简化调试操作）

项目内置GDB初始化脚本，自定义调试命令与输出格式：

1. **临时加载**（单次生效）

```bash
aarch64-linux-gnu-gdb -x scripts/.gdbinit output/vision_ai_app
```

2. **全局自动加载**（推荐，永久生效）

编辑PC端用户目录 `.gdbinit`，添加脚本路径：

```bash
vim ~/.gdbinit
# 添加内容
source 你的项目绝对路径/scripts/.gdbinit
```

### 7.4 硬件加速专项调试

```bash
# 查看RGA硬件运行状态
cat /sys/kernel/debug/rga/status

# 查看MPP编码占用
cat /sys/kernel/debug/mpp/status

# 查看NPU驱动版本与运行状态
cat /sys/devices/platform/rknpu/info
```

---

## 八、常见问题速查

1. **硬件加速库找不到 / 程序启动报库缺失**
   排查：`set_env.sh` 中库路径是否正确、NFS同步是否完整、动态库是否为 aarch64 架构；优先使用脚本内置环境加载，不手动配置。

2. **硬件节点不存在 / 硬件加速调用失败**
   排查：内核是否开启RGA/MPP驱动配置；确认`lsmod`能查到对应驱动模块。
   - RK3562实际验证：`/dev/rga`、`/dev/mpp_service`、`/dev/video-enc0` 均存在且可用
   - NPU节点（`/dev/rknpu`）不存在问题见"4.3 NPU设备节点特殊说明"

3. **摄像头打开失败 / 找不到设备节点**
   排查：量产模式确认rc.local中是否预留USB枚举延时；RK3562视频节点较多，执行 `cat /sys/class/video4linux/video*/name` 定位名称含 `USB Camera` 的节点，一般为 `/dev/video18`。

4. **NPU推理报错 / 模型加载失败**
   排查：检查 `/dev/rknpu` 设备节点是否存在
   - 设备节点不存在时：使用 MNN 软件推理作为替代方案
   - 解决方案：更换为MNN软件推理，或修复设备树启用NPU节点

5. **RTSP拉流超时/黑屏**
   排查：两端IP是否同网段、Windows防火墙是否关闭、端口8554是否被占用、VPU编码输出格式是否与live555匹配。

6. **驱动加载报错 File exists**
   正常现象：系统已内置对应驱动，脚本已添加错误屏蔽，不影响功能，无需处理。

7. **守护进程重启雪崩**
   排查：看门狗脚本重启延时、检测间隔配置，默认3s间隔可有效防止雪崩。

8. **VPU编码花屏 / 颜色异常**
   排查：输入图像格式是否为NV12（VPU原生支持格式）、图像宽高是否按硬件要求对齐、内存是否为物理连续内存。

---

## 九、板端目录结构

```
/root/run_on_board_rk3562/
├── vision_ai_app          # 主程序
├── auto_rk/               # 脚本目录
│   ├── set_env.sh         # 环境变量设置
│   ├── app_start.sh        # 启动脚本
│   ├── app_stop.sh        # 停止脚本
│   ├── app_watchdog.sh    # 看门狗脚本
│   ├── cpu_monitor.sh     # CPU监控脚本
│   ├── log_rotate.sh      # 日志轮转脚本
│   └── rc.local           # 开机自启脚本
├── rga/                   # RGA 图像加速库
│   └── librga.so*
├── rkmpp/                 # MPP 视频编解码库
│   └── *.so*
├── rknn/                 # RKNN NPU库（当前不可用）
│   └── *.so*
├── libjpeg/              # libjpeg-turbo 图像处理库
│   └── *.so*
├── mnn/                  # MNN 推理库
│   └── libMNN.so*
└── ultraface.mnn         # MNN AI模型文件
```

---

## 十、开发板实际环境验证记录

### 10.1 系统信息（2026-06-21验证）

```
开发板型号: YM3562 (RK3562核心板)
操作系统:   Ubuntu 22.04 LTS (Jammy)
内核版本:   6.1.99
架构:       aarch64
内存:       1.9GiB
存储:       27GB eMMC + NFS
```

### 10.2 硬件加速节点验证结果

| 设备节点 | 功能 | 验证状态 | 说明 |
|---------|------|---------|------|
| `/dev/rga` | RGA图像加速 | ✅ 正常 | 可用 |
| `/dev/mpp_service` | MPP视频编解码 | ✅ 正常 | 可用 |
| `/dev/video-enc0` | VPU硬件编码 | ✅ 正常 | 可用 |
| `/dev/mali0` | Mali GPU | ✅ 正常 | 可用 |
| `/dev/rknpu` | NPU推理 | ❌ 不存在 | 驱动未创建设备 |

### 10.3 摄像头设备

```
/dev/video18  - USB Camera (实际使用)
/dev/video0-19 - V4L2设备节点
/dev/video-dec0 - 视频解码器
/dev/video-enc0 - 视频编码器
```

### 10.4 已知问题

1. **NPU设备节点缺失**：`/dev/rknpu` 不存在，导致RKNN模型无法初始化
   - 内核配置已启用，但驱动未创建设备节点
   - 临时方案：使用MNN软件推理（CPU）

---

## 十一、工具链对比说明

| 工具链 | 版本 | C库 | 适用场景 | 推荐度 |
|--------|------|-----|---------|--------|
| `arm64-linux-103` | GCC 10.3 | musl/none | RK3562 专用、嵌入式优化 | ⭐⭐⭐⭐⭐ |
| `arm64-linux-75` | Linaro 7.5 | glibc | 通用 ARM64、需要 glibc | ⭐⭐⭐⭐ |

**选择建议**：
- **优先使用 `arm64-linux-103`**：专为 RK3562 优化，musl 库体积更小，适合嵌入式
- **使用 `arm64-linux-75`**：如果需要在 RK3562 上运行其他 glibc 程序，或需要更好的兼容性
