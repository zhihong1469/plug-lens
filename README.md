# 快速上手(示例或长命令便携)
环境(自行配置):
use_toolchain arm32-linux-hf6ull 
编译:
 make clean && make
传输到开发板(示例):
arm-buildroot-linux-gnueabihf-readelf -d output/vision_ai_app | grep NEEDED
# 复制 libcrypto + libssl 到你的板端运行目录
cp /usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/arm-buildroot-linux-gnueabihf/sysroot/usr/lib/libcrypto.so.1.1 ~/nfs/run_on_board/
cp /usr/local/arm/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/arm-buildroot-linux-gnueabihf/sysroot/usr/lib/libssl.so.1.1 ~/nfs/run_on_board/

cp output/vision_ai_app ~/nfs/run_on_board/
cp third_lib/face_detector/model/version-RFB/RFB-320-quant-KL-5792.mnn ~/nfs/run_on_board/
cp -rf  third_lib/face_detector/mnn/lib/libMNN.so ~/nfs/run_on_board
cp -rf  third_lib/libjpeg_turbo/lib/*.so* ~/nfs/run_on_board/libjpeg
<!-- cp third_lib/opencv_lib/lib/*.so.*  ~/nfs/run_on_board/opencv -->
cp -rf third_lib/openh264/lib/* ~/nfs/run_on_board/openh264
cp -rf third_lib/libyuv/lib/*  ~/nfs/run_on_board/libyuv

[root@100ask:/mnt/run_on_board]#
mount -t nfs -o nolock,port=2050 192.168.5.10:/home/luo/nfs /mnt
date -s "2026-05-22 12:00:00"
cd /mnt/run_on_board/
# 检查LED驱动:
lsmod | grep led
```shell
# 1. 开发板卸载旧模块
rmmod board_A_led
rmmod chip_demo_gpio
rmmod leddrv

# 4. 开发板按顺序加载
insmod leddrv.ko
insmod chip_demo_gpio.ko
insmod board_A_led.ko
ls /dev/100ask_led0
# 5. 测试LED
./ledtest /dev/100ask_led0 on   # 亮
./ledtest /dev/100ask_led0 off  # 灭
```
# 一次性添加所有库路径，大小写正确，永久生效当前终端
export LD_LIBRARY_PATH=/mnt/run_on_board/openh264:/mnt/run_on_board/libjpeg:/mnt/run_on_board/mnn:/mnt/run_on_board/libyuv:$LD_LIBRARY_PATH
<!-- export LD_LIBRARY_PATH=/mnt/run_on_board/opencv:$LD_LIBRARY_PATH -->



mkdir -p /mnt/sdcard/face_capture
mount /dev/mmcblk0p1 /mnt/sdcard # 具体看plugins/base_plugins/sd_storage/inc/sd_storage.h:SD_STORAGE_ROOT_PATH 结束拔卡前记得umount /mnt/sdcard
./vision_ai_app

sudo rm -f ~/nfs/face_capture/*.jpg
ffplay rtsp://192.168.5.9:8554/stream
ffplay -rtsp_transport udp -sync video -flags low_delay rtsp://192.168.5.9:8554/stream
GDB调试:
arm-buildroot-linux-gnueabihf-gdb ./output/vision_ai_app
[root@100ask:/mnt/run_on_board]# 
./gdbserver --once :12345 ./vision_ai_app
WSL2:
  target remote 192.168.5.9:12345
  thread apply all bt
Windows:
CMD（管理员）一键关闭所有网络防火墙:
  netsh advfirewall set allprofiles state off
# 结构速通(详细见documents/architecture.md)
一般应用层只需要专注plugins目录下的代码修改，src实现接口函数或者架构相关
ex:
新增功能需要实现device层代码，xxx_device.c的原代码也放plugins目录下，xxx_device.h放入src目录
## 注意
    六层架构调用顺序(从上到下)
    应用层 → 服务层 → 数据链路层 → 硬件抽象层 → 插件层 → 硬件
    编译顺序铁律
    common(公共库) → plugins(硬件实现) → src(核心框架)

# 项目开发细节(详细见documents/details.md)

# 工程目录可选项
## 文档利用(可选/documents)
documents/cur_issue.md 可用于记录当前代码开发流程或问题，或者方便问题或工作被快速交接(以往所有git提交记录均可查)
documents/debug.txt 用于拷贝记录开发板或编译调式问题日志

## 脚本(可选/scripts)
批量打印当前工程代码到指定文本，方便遇到bug或审查低级错误——快速利用AI工具寻找灵感。
scripts/print_core_src.sh 
scripts/print_all_src.sh
GDB配置脚本
scripts/.gdbinit 
- 使用方法(自选):
方案 1:临时加载(每次启动 GDB 指定)
```bash
gdb -x scripts/.gdbinit 你的程序
```
方案 2:自动加载(一劳永逸，推荐)
```bash
# 编辑 ~/.gdbinit，添加这行(路径写你实际的绝对/相对路径)
source ~/你的项目路径/scripts/.gdbinit
```
## .tool(外部开源工具集合)
ex:.tool/gdb-12.1.tar.xz 用于开发板资源较小无预装GDB，可考虑静态链接gdbserver

##


## 致谢与第三方依赖
本项目基于以下优秀开源项目开发，在此向所有贡献者表示感谢：

### 核心依赖库
| 项目 | 版本 | 许可证 | 用途 |
|------|------|--------|------|
| [MNN](https://github.com/alibaba/MNN) | 2.4.0 | Apache 2.0 | AI推理引擎 |
| [Ultra-Light-Fast-Generic-Face-Detector-1MB](https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB) | - | MIT | 轻量级人脸检测模型 |
| [Live555](http://www.live555.com/liveMedia/) | - | LGPL 2.1 | RTSP流媒体服务器 |
| [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) | 3.1.4.1 | BSD 3-clause + IJG | JPEG图像编解码 |
| [OpenH264](https://github.com/cisco/openh264) | - | BSD 2-clause | H.264视频编解码 |
| [libyuv](https://github.com/lemenkov/libyuv) | - | BSD 3-clause | YUV格式转换与缩放 |

### 工具与参考
- GDB 12.1（调试工具）
  - 源码包：https://ftp.gnu.org/gnu/gdb/gdb-12.1.tar.xz
  - 许可证：GPL-3.0-or-later
  - 用途：用于嵌入式 Linux 远程调试（.tool/gdb-12.1.tar.xz）
- [FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds)：PC端视频测试工具
- [兆鸣嵌入式](https://github.com/ZhaoChengBo/zhaoming-embedded)：C语言面向对象编程思想与工程实践参考
- [OpenCV](https://github.com/opencv/opencv) | 4.5.5 | Apache 2.0 | 图像调试处理时使用 |
## 第三方许可证说明
- 本项目自身代码基于 **MIT License** 开源
- 所有第三方依赖库均遵循其各自的开源许可证
- 完整的第三方许可证文件可在 `third_lib/` 目录下对应库的文件夹中找到