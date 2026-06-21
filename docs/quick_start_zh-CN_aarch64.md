# RK3562 视觉AI采集终端 开发&运行使用手册
**文档用途**：个人内部使用手册，整合交叉编译、NFS文件传输、双网络模式配置、硬件加速环境校验、脚本启停、RTSP拉流、调试排错全流程
**运行环境**：RK3562开发板(YM3562) + Ubuntu 22.04 LTS + sysvinit，架构为aarch64
**工程目录**：板端主目录 `/root/run_on_board_aarch64`，NFS共享目录 `~/nfs/run_on_board_aarch64`

---

## 一、整体流程总览
1. PC端：配置aarch64交叉编译工具链 → 指定RK3562平台编译工程 → 拷贝可执行文件、RKNN模型、硬件加速库及所有第三方依赖至NFS共享目录
2. 开发板端：二选一配置网络（以太网直连PC / 接入局域网路由器）→ 挂载NFS → 同步文件至板端固定目录
3. 板端前置操作：校验硬件加速节点权限、系统时间兜底设置、SD卡挂载（程序内部已适配，外部为可选兜底操作）
4. 程序运行：区分**手动启停脚本（调试）**、**开机自启（量产）**两种模式
5. 上层拉流：根据开发板IP网段，使用ffplay拉取RTSP视频流播放
6. 调试排错：远程GDBServer调试、防火墙处理、日志查看、硬件加速状态校验

---

## 二、PC端操作（编译 + 文件传输）
### 2.1 交叉编译工具链配置
本工程基于aarch64架构交叉编译工具链，编译前确认工具链环境已加载：
```shell
    Available Toolchains:
    ===== Zephyr RTOS =====
      use_toolchain zephyr              # Zephyr SDK 1.0.1 full

    ===== ARM32 Linux glibc =====
      use_toolchain arm32-linux-hf6ull  # i.MX6ULL Buildroot glibc HF
      use_toolchain arm32-linux-hf16    # Linaro 6.2 glibc HF
      use_toolchain arm32-linux-sf17    # Linaro 5.5 glibc SF

    ===== ARM32 Bare-metal =====
      use_toolchain arm32-bare-20       # Cortex-M GCC 10.2 (Old)
      use_toolchain arm32-bare-26       # Cortex-M GCC 15.2 (New)

    ===== ARM64 (RK3562) =====
      use_toolchain arm64-bare-26       # ARM64 Bare-metal GCC 15.2
      use_toolchain arm64-linux-75      # RK3562 Linux glibc 7.5 ✅日常用
      use_toolchain arm64-linux-103     # RK3562 Linux musl/none 10.3
luo@Luo1469:~/linux/6ull/project/plug-lens$ use_toolchain arm64-linux-103
```
```bash
# 加载Linaro aarch64 7.5或者官方推荐的10.3交叉编译工具链（根据自身实际路径执行）
export PATH=/usr/local/arm/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin:$PATH
export PATH=/usr/local/arm/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc:$PATH
```

### 2.2 工程编译
进入项目根目录，指定RK3562平台执行清理+编译：
```bash
make clean && make PLATFORM=rk3562
```
> 工程构建已内置头文件与依赖库检查逻辑，正常直接make即可；若出现依赖缺失可先检查第三方库路径配置。

### 2.3 依赖库检查
编译完成后，检查可执行文件依赖的动态库，提前确认库完整性：
```bash
aarch64-linux-gnu-readelf -d output/vision_ai_app | grep NEEDED
```
> 请确保所有动态库均已完成aarch64架构交叉编译，与工具链版本匹配。

### 2.4 批量拷贝文件至NFS共享目录
将**可执行程序、AI模型、硬件加速库、所有第三方动态库**统一拷贝到PC端NFS共享目录 `~/nfs/run_on_board_aarch64`，开发板通过NFS挂载访问：
```bash
# 创建目录结构
mkdir -p ~/nfs/run_on_board_aarch64/{libjpeg,drv,rknn,rkmpp,auto_rk}

# 1. 拷贝主程序
cp output/vision_ai_app ~/nfs/run_on_board_aarch64/

# 2. 拷贝启动脚本
cp scripts/auto_rk/*.sh ~/nfs/run_on_board_aarch64/auto_rk/
chmod +x ~/nfs/run_on_board_aarch64/auto_rk/*.sh

# 3. 拷贝AI模型文件（RKNN量化版，适配RK3562 NPU）
cp third_lib/aarch64/face_detector/model/ultraface_rk3562.rknn ~/nfs/run_on_board_aarch64/

# 4. 拷贝RK3562硬件加速库
cp -a third_lib/rk3562/libjpeg_turbo/lib/*.so* ~/nfs/run_on_board_aarch64/libjpeg/
cp -a third_lib/rk3562/rkrga/lib/*.so* ~/nfs/run_on_board_aarch64/
cp -a third_lib/rk3562/rkmpp/lib/*.so* ~/nfs/run_on_board_aarch64/drv/
cp -a third_lib/rk3562/rknn/lib/*.so* ~/nfs/run_on_board_aarch64/rknn/
```

---

## 三、开发板网络配置 & NFS挂载（两种网段模式）
> 核心区分：两种网络模式对应不同IP网段、NFS挂载地址、RTSP拉流地址，根据使用场景二选一
>
> 板端固定工作目录：`/root/run_on_board_aarch64`（脚本、守护进程已适配此路径，**禁止修改**）
> NFS挂载参数统一：`nolock,port=2050`，适配Buildroot裁剪系统

### 3.1 模式一：以太网直连PC（纯开发调试---主力）
适用场景：仅本机PC和开发板联调，无其他设备接入
1. 网段规划：PC网口 & 开发板eth0 统一网段 `192.168.5.x`
   - PC IP：`192.168.5.10`
   - 开发板IP：`192.168.5.11`
2. 开发板执行NFS挂载与文件同步：
```bash
# 挂载PC端NFS共享目录至板端 /mnt
# mount -t nfs -o nolock,port=2050 192.168.5.10:/home/luo/nfs /mnt 被别名为mount_nfs_wired
mount_nfs_wired
# 将NFS内完整项目文件同步至板端固定目录
mkdir -p /root/run_on_board_aarch64
cp -rp /mnt/run_on_board_aarch64/* /root/run_on_board_aarch64/

# 给自动化脚本赋予执行权限（首次部署必执行）
chmod +x /root/run_on_board_aarch64/auto_rk/*.sh
```

### 3.2 模式二：接入路由器局域网（多设备拉流播放---临时）
适用场景：开发板、PC、手机等多设备接入同一WiFi/路由器，任意设备均可拉取RTSP流
1. 网段规划：所有设备统一路由器网段 `10.168.1.x`
   - PC NFS服务IP：`10.168.1.173`
   - 开发板IP：路由器自动分配或手动设置同网段IP
2. 开发板执行网络配置与NFS挂载：
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
mkdir -p /root/run_on_board_aarch64
cp -rp /mnt/run_on_board_aarch64/* /root/run_on_board_aarch64/
chmod +x /root/run_on_board_aarch64/auto_rk/*.sh
```

> 永久静态IP配置（需长期固定网段时使用）：
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
date -s "2026-06-14 15:30:00"
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
- **临时方案**：使用MNN软件推理（CPU）替代RKNN NPU

### 4.4 LED驱动加载测试（RK3562使用系统GPIO，无需额外驱动）
```bash
# RK3562 使用系统GPIO控制LED，无需加载额外驱动
echo "✅ RK3562 GPIO 已就绪"
```

### 4.5 环境变量说明
整套脚本已内置环境变量加载逻辑（`set_env.sh`），自动配置硬件加速库、第三方库的动态链接路径：
```bash
export APP_HOME="/mnt/nfs/run_on_board_aarch64"
export LD_LIBRARY_PATH=$APP_HOME/libjpeg:$APP_HOME/drv:$APP_HOME/rknn:$APP_HOME/rkmpp
```

---

## 五、程序启停方案（两套方案，区分场景）
### 5.1 方案一：手动脚本启停（日常调试，主力使用）
基于自动化脚本，一键启动/停止全套服务（业务程序+软件看门狗）
```bash
# 进入板端根目录
cd /root/run_on_board_aarch64

# 一键启动：自动切换目录、加载环境、拉起看门狗+业务程序
./auto_rk/app_start.sh &

# 一键停止：优雅关闭看门狗、业务进程，防止僵尸进程
./auto_rk/app_stop.sh

# 实时查看运行日志
tail -f /mnt/nfs/test/log/app.log
```

### 5.2 方案二：开机全自动自启（量产/无人值守）
依托系统 `sysvinit + rc.local` 实现上电自启，无需登录终端
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
适用于守护进程、后台程序、复杂问题定位，使用 `gdbserver` 远程联动
1. **开发板端**：启动gdbserver，绑定端口并拉起程序
```bash
# 端口 12345，单次连接模式
cd /root/run_on_board_aarch64
gdbserver --once :12345 ./vision_ai_app
```
2. **PC端**：连接远程gdbserver
```bash
# 进入PC端项目编译目录，启动aarch64交叉GDB
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
   排查：`set_env.sh` 中库路径是否正确、NFS同步是否完整、动态库是否为aarch64架构；优先使用脚本内置环境加载，不手动配置。

2. **硬件节点不存在 / 硬件加速调用失败**
   排查：内核是否开启RGA/MPP驱动配置；确认`lsmod`能查到对应驱动模块。
   - RK3562实际验证：`/dev/rga`、`/dev/mpp_service`、`/dev/video-enc0` 均存在且可用
   - NPU节点（`/dev/rknpu`）不存在问题见"4.3 NPU设备节点特殊说明"

3. **摄像头打开失败 / 找不到设备节点**
   排查：量产模式确认rc.local中是否预留USB枚举延时；RK3562视频节点较多，执行 `cat /sys/class/video4linux/video*/name` 定位名称含 `USB Camera` 的节点，一般为 `/dev/video18`。

4. **NPU推理报错 / 模型加载失败**
   排查：检查 `/dev/rknpu` 设备节点是否存在
   - 设备节点不存在时：`AI model initialization failed` - RKNN驱动未正确初始化
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

## 九、目录结构（实际）

```
/mnt/nfs/run_on_board_aarch64/
├── vision_ai_app          # 主程序
├── auto_rk/               # 脚本目录
│   ├── set_env.sh         # 环境变量设置
│   ├── app_start.sh       # 启动脚本
│   ├── app_stop.sh        # 停止脚本
│   ├── app_watchdog.sh    # 看门狗脚本
│   ├── cpu_monitor.sh     # CPU监控脚本
│   ├── log_rotate.sh      # 日志轮转脚本
│   ├── clear_sd_cache.sh  # 清理缓存脚本
│   ├── backup_all_to_tmp.sh # 备份脚本
│   └── rc.local           # 开机自启脚本
├── libjpeg/               # libjpeg-turbo 库
├── drv/                   # RKMPP/VPU 驱动库
├── rknn/                  # RKNN NPU 库
├── rkmpp/                 # RKMPP 编码库
├── librga.so              # RGA 硬件加速库
└── ultraface_rk3562.rknn  # RKNN量化AI模型
```

---

## 十、开发板实际环境验证记录

### 10.1 系统信息（2026-06-20验证）
```
开发板型号: YM3562 (RK3562核心板)
操作系统:   Ubuntu 22.04 LTS (Jammy)
内核版本:   6.1.99
架构:       aarch64
内存:       1.9GiB
存储:       27GB eMMC + 1007GB NFS
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
