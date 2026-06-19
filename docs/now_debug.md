# RK3562 开发板调试指南

## 一、PC端操作

```bash
# 复制可执行文件到 NFS 目录
cp output/vision_ai_app ~/nfs/run_on_board_rk3562/

# 复制脚本文件到 NFS 目录（创建 auto_rk 子目录）
mkdir -p ~/nfs/run_on_board_rk3562/auto_rk
cp scripts/auto_rk/*.sh ~/nfs/run_on_board_rk3562/auto_rk/

# 给脚本添加执行权限（NFS 权限由 PC 端控制）
chmod +x ~/nfs/run_on_board_rk3562/auto_rk/*.sh
```

## 二、板端操作

```bash
mount_nfs_wired
# 设置环境变量
export APP_HOME="/mnt/nfs/run_on_board_rk3562"
export LD_LIBRARY_PATH=$APP_HOME/openh264:$APP_HOME/libjpeg:$APP_HOME/mnn:$APP_HOME/libyuv

# 进入工作目录
cd /mnt/nfs/run_on_board_rk3562/

# 查看文件列表
ls
# auto_rk  libjpeg  libyuv  mnn  openh264  RFB-320-quant-KL-5792.mnn  vision_ai_app

# 手动启动程序
./vision_ai_app

# 手动停止程序
killall -9 vision_ai_app

# 使用脚本启动（推荐）
./auto_rk/app_start.sh

# 使用脚本停止
./auto_rk/app_stop.sh

# 手动启动 CPU 监控（可选）
./auto_rk/cpu_monitor.sh &

# 手动启动看门狗（可选）
./auto_rk/app_watchdog.sh &
```

## 三、自动启动配置

```bash
# 将 rc.local 复制到开发板
cp /mnt/nfs/run_on_board_rk3562/auto_rk/rc.local /etc/rc.local

# 设置权限
chmod +x /etc/rc.local

# 重启测试
reboot
```

## 四、目录结构

```
/mnt/nfs/run_on_board_rk3562/
├── vision_ai_app          # 主程序
├── auto_rk/               # 脚本目录
│   ├── set_env.sh         # 环境变量设置
│   ├── app_start.sh       # 启动脚本
│   ├── app_stop.sh        # 停止脚本
│   ├── app_watchdog.sh    # 看门狗脚本
│   ├── cpu_monitor.sh     # CPU监控脚本
│   └── rc.local           # 开机自启脚本
├── openh264/              # OpenH264 库
├── libjpeg/               # libjpeg-turbo 库
├── mnn/                   # MNN 库
├── libyuv/                # libyuv 库
└── RFB-320-quant-KL-5792.mnn  # AI 模型文件
```

## 五、日志路径

```
/mnt/nfs/test/log/
├── app.log                # 主程序日志
├── watchdog.log           # 看门狗日志
└── cpu_status.log         # CPU监控日志
```

## 六、常用命令

```bash
# 查看主程序日志
tail -f /mnt/nfs/test/log/app.log

# 查看看门狗日志
tail -f /mnt/nfs/test/log/watchdog.log

# 查看 CPU 监控日志
tail -f /mnt/nfs/test/log/cpu_status.log

# 查看进程状态
ps | grep vision_ai_app

# 查看所有相关进程
ps aux | grep -E "vision_ai|cpu_monitor|app_watchdog"

# 查看内存使用
free -h

# 查看 CPU 使用率（交互模式）
top

# 查看 CPU 使用率（批处理模式，适合脚本）
top -b -n 1

# 查看主程序 CPU 占用
top -b -n 1 | grep vision_ai_app
```

## 七、关闭进程方法

```bash
# 方法1：使用停止脚本（推荐）
./auto_rk/app_stop.sh

# 方法2：关闭单个进程
killall -9 vision_ai_app
killall -9 cpu_monitor.sh
killall -9 app_watchdog.sh

# 方法3：一键关闭所有相关进程
killall -9 vision_ai_app cpu_monitor.sh app_watchdog.sh

# 方法4：先关闭看门狗（避免自动重启），再关闭其他进程
killall -9 app_watchdog.sh
killall -9 cpu_monitor.sh
killall -9 vision_ai_app

# 验证关闭结果
ps aux | grep -E "vision_ai|cpu_monitor|app_watchdog"
```

## 八、调试技巧

```bash
# 查看程序是否在运行
pgrep vision_ai_app

# 查看程序启动时间和运行时长
ps -eo pid,lstart,etime,cmd | grep vision_ai_app

# 查看系统负载
uptime

# 查看网络连接
netstat -tlnp | grep vision_ai_app

# 查看 GPU 状态（RK3562）
cat /sys/kernel/debug/rga/status

# 查看 NPU 状态（RK3562）
cat /sys/kernel/debug/rknpu/status
```