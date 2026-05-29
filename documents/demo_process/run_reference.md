# i.MX6ULL 视觉AI采集终端 开发&运行使用手册
**文档用途**：个人内部使用手册，整合交叉编译、NFS文件传输、双网络模式配置、驱动加载、脚本启停、RTSP拉流、调试排错全流程
**运行环境**：100ask i.MX6ULL + 裁剪版Buildroot + BusyBox + sysvinit（无systemd）
**工程目录**：板端主目录 `/root/run_on_board`，NFS共享目录 `~/nfs/run_on_board`

---

## 一、整体流程总览
1. PC端：配置交叉编译工具链 → 编译工程 → 拷贝可执行文件、模型、第三方库至NFS共享目录
2. 开发板端：二选一配置网络（USB直连PC / 接入局域网路由器）→ 挂载NFS → 同步文件至板端固定目录
3. 板端前置操作：加载内核驱动、时间兜底设置、SD卡挂载（程序内部已适配，外部可选操作）
4. 程序运行：区分**手动启停脚本（调试）**、**开机自启（量产）**两种模式
5. 上层拉流：根据开发板IP网段，使用ffplay为例拉取RTSP视频流播放
6. 调试排错：本地GDB、远程GDBServer、防火墙处理、日志查看等

---

## 二、PC端操作（编译 + 文件传输）
### 2.1 交叉编译工具链配置
本工程基于ARM32交叉编译工具链，编译前加载工具链环境：
```bash
# 加载交叉编译工具链（根据自身环境执行）
use_toolchain arm32-linux-hf6ull
```

### 2.2 工程编译
进入项目根目录，执行清理+编译：
```bash
make clean && make
# 我们的项目构建逻辑支持头文件检查,按理也包括链接库检查,一般直接make可以,但是如果出现问题可以给我反馈
```

### 2.3 依赖库检查
编译完成后，检查可执行文件依赖的动态库，提前确认库完整性：
```bash
arm-buildroot-linux-gnueabihf-readelf -d xxxxxxxxxxxx | grep NEEDED
```
>请确保库的交叉编译与您使用的工具链一致

### 2.4 批量拷贝文件至NFS共享目录
将**可执行程序、AI模型、所有第三方动态库**统一拷贝到PC端NFS共享目录 `~/nfs/run_on_board`，开发板通过NFS挂载访问：
```bash
mkdir -p ~/nfs/run_on_board/{mnn,libjpeg,openh264,libyuv,drv}
# 1. 拷贝主程序
cp output/vision_ai_app ~/nfs/run_on_board/

cp -rf drv/led_drv/*.ko  ~/nfs/run_on_board/drv
# 2. 拷贝MNN AI模型文件
cp third_lib/face_detector/model/version-RFB/RFB-320-quant-KL-5792.mnn ~/nfs/run_on_board/

# 3. 拷贝第三方动态库（按目录分类）
cp -rf third_lib/face_detector/mnn/lib/libMNN.so ~/nfs/run_on_board/mnn
cp -rf third_lib/libjpeg_turbo/lib/*.so* ~/nfs/run_on_board/libjpeg
cp -rf third_lib/openh264/lib/* ~/nfs/run_on_board/openh264
cp -rf third_lib/libyuv/lib/*  ~/nfs/run_on_board/libyuv

########################  仅供参考/可选项：########################################
# 4. OpenCV库（按调试需开启）
cp third_lib/opencv_lib/lib/*.so.*  ~/nfs/run_on_board/opencv
# 5. 拷贝SSL/加密依赖库（按需开启）
cp /usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/arm-buildroot-linux-gnueabihf/sysroot/usr/lib/libcrypto.so.1.1 ~/nfs/run_on_board/
cp /usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/arm-buildroot-linux-gnueabihf/sysroot/usr/lib/libssl.so.1.1 ~/nfs/run_on_board/
# 6. 拷贝自动化脚本目录（守护进程、启停脚本等）
cp -rf scripts/auto ~/nfs/run_on_board/


```

---

## 三、开发板网络配置 & NFS挂载（两种网段模式）
> 核心区分：两种网络模式对应不同IP网段、NFS挂载地址、RTSP拉流地址，根据使用场景二选一

### 前置说明
- 板端固定工作目录：`/root/run_on_board`（脚本、守护进程已适配此路径，**禁止修改**）
- NFS挂载参数统一：`nolock,port=2050`，适配Buildroot裁剪系统

### 3.1 模式一：USB网口直连PC（纯开发调试---主力）
适用场景：仅本机PC和开发板联调，无其他设备接入
1. 网段规划：PC USB网卡 & 开发板 统一网段 `192.168.5.x`
   - PC IP：`192.168.5.10`
   - 开发板IP：`192.168.5.9`
2. 开发板执行NFS挂载：
```bash
# 已永久设置
vi /etc/network/interfaces
# 挂载PC端NFS共享目录至板端 /mnt
mount -t nfs -o nolock,port=2050 192.168.5.10:/home/luo/nfs /mnt

# 将NFS内完整项目文件同步至板端固定目录 如:/root/run_on_board
mkdir run_on_board
cp -rp /mnt/run_on_board /root/

# cp -rp /mnt/run_on_board/vision_ai_app /root/run_on_board/
chmod +x /root/run_on_board/auto/*.sh
```

### 3.2 模式二：接入路由器局域网（多设备拉流播放---临时）
适用场景：开发板、PC、手机等多设备接入同一WiFi/路由器，任意设备均可拉取RTSP流
1. 网段规划：所有设备统一路由器网段 `10.168.1.x`
   - PC NFS服务IP：`10.168.1.173`
   - 开发板IP：路由器分配同网段IP
2. 开发板执行NFS挂载：
```bash
# 1. 临时把IP改成和电脑同网段（10.168.1.x，不冲突就行）
ifconfig eth0 10.168.1.9 netmask 255.255.255.0

# 2. 临时设置网关（和你电脑的网关一致：10.168.1.1）
route add default gw 10.168.1.1 eth0
# 3. 临时配置DNS（用路由器网关+公网DNS）
echo "nameserver 10.168.1.1" > /etc/resolv.conf
echo "nameserver 223.5.5.5" >> /etc/resolv.conf
# 可选验证:
ping 10.168.1.1   # 先ping网关，确认局域网通
ping 223.5.5.5    # 再ping公网DNS，确认能上外网
# 挂载路由器网段下的PC NFS共享目录
mount -t nfs -o nolock,port=2050 10.168.1.173:/home/luo/nfs /mnt
# 同步文件至板端固定目录
cp -rp /mnt/run_on_board /root
```
```bash
# 清空原有全部内容,类似如下:
auto lo
iface lo inet loopback

# 以太网eth0 静态IP 永久配置
auto eth0
iface eth0 inet static
    address 192.168.5.9
    netmask 255.255.255.0
    gateway 192.168.5.10
    dns-nameservers 114.114.114.114
# 永久生效备用---把文件里的 eth0 配置改成下面这样（固定 IP + 网关 + DNS）:
auto eth0
iface eth0 inet static
    address 10.168.1.9       # 和临时设置的IP一致，不冲突就行
    netmask 255.255.255.0
    gateway 10.168.1.1       # 你的网关，和电脑一致
    dns-nameservers 223.5.5.5 114.114.114.114
```
---

## 四、开发板前置初始化操作（通用流程）
> 说明：程序内部已实现SD卡自动挂载、目录创建，**外部挂载为可选操作**，重复挂载不会报错；时间设置为手动兜底（程序内部自带网络时间获取逻辑）

### 4.1 系统时间兜底设置
两种网络模式下均建议手动校准时间，避免日志、抓拍文件时间错乱：
```bash
date -s "2026-05-29 00:00:00"
```

### 4.2 手动SD卡挂载（可选）
程序内部已适配SD卡挂载逻辑，外部手动执行仅作兜底：
```bash
# 创建抓拍目录（兜底）
mkdir -p /mnt/sdcard/face_capture

# 手动挂载SD卡（程序已实现，可选执行）
mount /dev/mmcblk0p1 /mnt/sdcard

# 重要：拔卡/断电前务必卸载，防止文件损坏
umount /mnt/sdcard
```

### 4.3 LED内核驱动加载测试（运行前必测试）
严格遵循 **卸载旧驱动 → 按顺序加载新驱动 → 设备节点校验**，适配裁剪版内核模块规则
```bash
# 1. 卸载已加载的旧驱动（避免重复加载报错）
rmmod /root/run_on_board/drv/board_A_led 2>/dev/null
rmmod /root/run_on_board/drv/chip_demo_gpio 2>/dev/null
rmmod /root/run_on_board/drv/leddrv 2>/dev/null

# 2. 按顺序加载驱动（固定加载顺序，不可颠倒）
insmod /root/run_on_board/drv/leddrv.ko 2>/dev/null
insmod /root/run_on_board/drv/chip_demo_gpio.ko 2>/dev/null
insmod /root/run_on_board/drv/board_A_led.ko 2>/dev/null

# 3. 校验驱动是否加载成功
lsmod | grep led
ls /dev/100ask_led0

# 4. 驱动功能测试（可选）
/root/run_on_board/drv/ledtest /dev/100ask_led0 on   # LED点亮
/root/run_on_board/drv/ledtest /dev/100ask_led0 off  # LED熄灭
```

### 4.4 脚本赋权（首次部署必执行）
自动化启停脚本、看门狗脚本需要执行权限，首次拷贝后配置：
```bash
chmod +x /root/run_on_board/auto/*.sh
```

> 补充：**环境变量说明**
> 整套脚本已内置环境变量加载逻辑（`set_env.sh`），守护进程/后台进程可自动继承，**无需手动执行 export 配置库路径**，废弃旧版手动配置方式。

---

## 五、程序启停方案（两套方案，区分场景）
### 5.1 方案一：手动脚本启停（日常调试，主力使用）
基于前期编写的自动化脚本，一键启动/停止全套服务（业务程序+软件看门狗）
```bash
# 进入根目录
cd /root

# 一键启动：自动切目录、加载环境、拉起看门狗+业务程序
./run_on_board/auto/app_start.sh &

# 一键停止：优雅关闭看门狗、业务进程（防僵尸进程）
./run_on_board/auto/app_stop.sh

tail -f /mnt/test/log/app.log
```

### 5.2 方案二：开机全自动自启（量产/无人值守）
依托系统 `sysvinit + rc.local` 实现上电自启，**无需登录终端**
1. 编辑 `/etc/rc.local`，使用前文【量产模板】配置（内置延时、环境变量、驱动加载、调用启动脚本）
2. 确认 `/etc/inittab` 配置不变（仅保留 `rc01::sysinit:/etc/rc.local` 扩展项，不修改登录配置）
3. 重启开发板，上电后系统自动完成：挂载SD卡 → 加载驱动 → 加载环境 → 启动全套服务

---

## 六、PC端 RTSP 拉流播放（分两种网络模式）
使用 `ffplay` 拉取开发板RTSP视频流，IP地址跟随开发板网段切换，支持普通模式与低延迟模式

### 6.1 模式一：USB直连PC（网段 192.168.5.x）
开发板IP：`192.168.5.9`
```bash
# 常规拉流播放
ffplay rtsp://192.168.5.9:8554/stream

# 低延迟播放（UDP传输，优先音视频同步，推荐实时预览）
ffplay -rtsp_transport udp -sync video -flags low_delay rtsp://192.168.5.9:8554/stream
```

### 6.2 模式二：路由器局域网（网段 10.168.1.x）
将命令中IP替换为**开发板当前局域网IP**，手机、平板等同网段设备均可使用此地址拉流：
```bash
# 替换为开发板实际IP，示例如下
ffplay rtsp://10.168.1.XXX:8554/stream
ffplay -rtsp_transport udp -sync video -flags low_delay rtsp://10.168.1.XXX:8554/stream
```

---

## 七、调试方案汇总（GDB、防火墙、脚本调试）
### 7.1 本地GDB调试（PC端，前台运行调试）
适用于程序前台启动、简单问题定位
```bash
# 进入项目输出目录，启动本地GDB
arm-buildroot-linux-gnueabihf-gdb output/vision_ai_app
```

### 7.2 远程GDB调试（开发板+PC，核心调试方案）
适用于守护进程、后台程序、复杂问题定位，使用 `gdbserver` 远程联动
1. **开发板端**：启动gdbserver，绑定端口并拉起程序
```bash
# 端口 12345，单次连接模式
./gdbserver --once :12345 ./vision_ai_app
```
2. **PC/WSL2端**：连接远程gdbserver
```bash
# 进入PC端编译目录，启动交叉gdb
arm-buildroot-linux-gnueabihf-gdb output/vision_ai_app

# 连接开发板（IP根据网络模式切换）
# USB直连模式
target remote 192.168.5.9:12345
# 路由器局域网模式
target remote 10.168.1.XXX:12345

# 常用调试命令
thread apply all bt  # 查看所有线程调用栈（排查崩溃首选）
```

### 7.3 Windows 防火墙处理（拉流/远程调试失败优先排查）
Windows防火墙会拦截/限速 RTSP流、GDB远程端口，调试前关闭防火墙：
> 以**管理员身份**打开CMD执行：
```cmd
netsh advfirewall set allprofiles state off
```

### 7.4 GDB配置脚本（简化调试操作）
项目内置GDB初始化脚本，自定义调试命令、格式：
1. **临时加载**（单次生效）
```bash
arm-buildroot-linux-gnueabihf-gdb -x scripts/.gdbinit output/vision_ai_app
```
2. **全局自动加载**（推荐，永久生效）
编辑PC端用户目录 `.gdbinit`，添加脚本路径：
```bash
vim ~/.gdbinit
# 添加内容
source 你的项目绝对路径/scripts/.gdbinit
```

### 7.5 代码批量导出脚本（问题排查辅助）
项目内置源码批量打印脚本，用于代码审查、AI辅助排错：
```bash
# 打印核心代码
bash scripts/print_core_src.sh

# 打印全部源码
bash scripts/print_all_src.sh
```

## 八、常见问题速查
1. **动态库找不到**
   排查：脚本是否完整拷贝、`set_env.sh` 路径是否正确；优先使用脚本内置环境加载，不手动配置。
2. **摄像头打开失败**
   排查：量产模式检查 `rc.local` 中 `sleep 5` 延时是否开启（等待USB枚举）；检查 `/dev/video0` 节点。
3. **RTSP拉流超时/黑屏**
   排查：两端IP网段一致、Windows防火墙关闭、端口8554未被占用。
4. **驱动加载报错 File exists**
   正常现象：原厂驱动已加载，脚本已添加 `2>/dev/null` 屏蔽报错，无需处理。
5. **守护进程重启雪崩**
   排查：看门狗脚本重启延时、检测间隔配置，默认3s间隔可正常防雪崩。