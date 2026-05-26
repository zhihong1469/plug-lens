## not have srv 
Mem: 198044K used, 305344K free, 2152K shrd, 4700K buff, 73336K cached
CPU:   8% usr  30% sys   0% nic  60% idle   0% io   0% irq   0% sirq
Load average: 0.53 0.58 0.38 2/110 8814
  PID  PPID USER     STAT   VSZ %VSZ %CPU COMMAND
  276     1 root     S     190m  39%  21% /usr/bin/mxapp2 --plugin tslib:/dev/input/event1
 8710  5372 root     R     2708   1%   1% top -b -n 7676


## only cap srv 
Mem: 198740K used, 304648K free, 1968K shrd, 4652K buff, 68636K cached
CPU:   5% usr  41% sys   0% nic  52% idle   0% io   0% irq   0% sirq
Load average: 0.66 0.55 0.49 1/111 19951
  PID  PPID USER     STAT   VSZ %VSZ %CPU COMMAND
  276     1 root     S     190m  39%  23% /usr/bin/mxapp2 --plugin tslib:/dev/input/event1
19531 14440 root     S    57196  11%   3% ./vision_ai_app
thread: app main capture

## cap net but no have client
Mem: 199532K used, 303856K free, 1968K shrd, 4652K buff, 68636K cached
CPU:   8% usr  43% sys   0% nic  47% idle   0% io   0% irq   0% sirq
Load average: 0.42 0.50 0.46 1/114 16643
  PID  PPID USER     STAT   VSZ %VSZ %CPU COMMAND
  276     1 root     S     190m  39%  25% /usr/bin/mxapp2 --plugin tslib:/dev/input/event1
16181 14440 root     S    77548  15%   7% ./vision_ai_app
thread: app main capture

## cap face
Mem: 220796K used, 282592K free, 2152K shrd, 4716K buff, 83924K cached
15fpsCPU:  91% usr   4% sys   0% nic   2% idle   0% io   0% irq   1% sirq
Load average: 2.95 1.25 0.70 1/113 425
  PID  PPID USER     STAT   VSZ %VSZ %CPU COMMAND
32736   279 root     S    89920  18%  90% ./vision_ai_app
Mem: 223496K used, 279892K free, 1956K shrd, 4716K buff, 87028K cached
 thread: app main capture=15=face

5fpsCPU:  78% usr  12% sys   0% nic   6% idle   0% io   0% irq   2% sirq
Load average: 2.32 1.17 0.84 1/113 6103
  PID  PPID USER     STAT   VSZ %VSZ %CPU COMMAND
 5953   279 root     S    89920  18%  78% ./vision_ai_app
 thread: app main capture=5=face

CPU:  72% usr  13% sys   0% nic  11% idle   0% io   0% irq   2% sirq
Load average: 2.45 1.46 1.01 2/114 7719
  PID  PPID USER     STAT   VSZ %VSZ %CPU COMMAND
 7580   279 root     S    89920  18%  72% ./vision_ai_app
thread: app main capture=15 face=5
```shell
Mem: 229460K used, 273928K free, 1964K shrd, 4716K buff, 90744K cached
CPU:   0% usr  25% sys   0% nic  50% idle   0% io   0% irq  25% sirq
Load average: 3.00 1.75 1.24 1/119 17899
  PID  PPID USER     STAT   VSZ %VSZ %CPU COMMAND
  276     1 root     S     190m  39%  17% /usr/bin/mxapp2 --plugin tslib:/dev/input/event1
17896 13403 root     R     2580   1%   8% top -b -n 1
  243     1 root     S<   97088  19%   0% /usr/bin/pulseaudio --system --daemonize --disallow-module-loading --disallow-exit --exit-idle-time=-1 --use-pid-file --disable-shm
17477   279 root     S    89920  18%   0% ./vision_ai_app
  264     1 root     S    41116   8%   0% /usr/bin/swupdate -f /etc/swupdate/swupdate.cfg -L -e rootfs,rootfs-1 -u
  246   243 root     S    19192   4%   0% /usr/libexec/pulse/gsettings-helper
  269   264 root     S    14480   3%   0% /usr/bin/swupdate -f /etc/swupdate/swupdate.cfg -L -e rootfs,rootfs-1 -u
  184     1 root     S    11324   2%   0% /sbin/udevd -d
  238     1 root     S     7900   2%   0% /usr/sbin/ntpd -g
32761   254 root     S     6304   1%   0% sshd: root@pts/0
13393   254 root     S     6304   1%   0% sshd: root@pts/1
  305   254 root     S     6000   1%   0% sshd: root@notty
13402   254 root     S     6000   1%   0% sshd: root@notty
  254     1 root     S     5880   1%   0% /usr/sbin/sshd
  323   305 root     S     5344   1%   0% /usr/libexec/sftp-server
13416 13402 root     S     5344   1%   0% /usr/libexec/sftp-server
[root@100ask:~]# top -b -n 1 | head -20
Mem: 229460K used, 273928K free, 1964K shrd, 4716K buff, 90744K cached
CPU:  27% usr  36% sys   0% nic  36% idle   0% io   0% irq   0% sirq
Load average: 3.00 1.75 1.24 2/119 17903
  PID  PPID USER     STAT   VSZ %VSZ %CPU COMMAND
  276     1 root     S     190m  39%  18% /usr/bin/mxapp2 --plugin tslib:/dev/input/event1
17901 13403 root     R     2580   1%  18% top -b -n 1
  243     1 root     S<   97088  19%   0% /usr/bin/pulseaudio --system --daemonize --disallow-module-loading --disallow-exit --exit-idle-time=-1 --use-pid-file --disable-shm
17477   279 root     S    89920  18%   0% ./vision_ai_app
  264     1 root     S    41116   8%   0% /usr/bin/swupdate -f /etc/swupdate/swupdate.cfg -L -e rootfs,rootfs-1 -u
  246   243 root     S    19192   4%   0% /usr/libexec/pulse/gsettings-helper
  269   264 root     S    14480   3%   0% /usr/bin/swupdate -f /etc/swupdate/swupdate.cfg -L -e rootfs,rootfs-1 -u
  184     1 root     S    11324   2%   0% /sbin/udevd -d
  238     1 root     S     7900   2%   0% /usr/sbin/ntpd -g
32761   254 root     S     6304   1%   0% sshd: root@pts/0
13393   254 root     S     6304   1%   0% sshd: root@pts/1
  305   254 root     S     6000   1%   0% sshd: root@notty
13402   254 root     S     6000   1%   0% sshd: root@notty
  254     1 root     S     5880   1%   0% /usr/sbin/sshd
  323   305 root     S     5344   1%   0% /usr/libexec/sftp-server
13416 13402 root     S     5344   1%   0% /usr/libexec/sftp-server
[root@100ask:~]# top -b -n 1 | head -20
Mem: 229780K used, 273608K free, 1964K shrd, 4716K buff, 90744K cached
CPU:  71% usr  21% sys   0% nic   7% idle   0% io   0% irq   0% sirq
Load average: 3.00 1.75 1.24 2/120 17909
  PID  PPID USER     STAT   VSZ %VSZ %CPU COMMAND
17477   279 root     S    89920  18%  66% ./vision_ai_app
  276     1 root     S     190m  39%  11% /usr/bin/mxapp2 --plugin tslib:/dev/input/event1
17905 13403 root     R     2580   1%   3% top -b -n 1
  243     1 root     S<   97088  19%   0% /usr/bin/pulseaudio --system --daemonize --disallow-module-loading --disallow-exit --exit-idle-time=-1 --use-pid-file --disable-shm
  264     1 root     S    41116   8%   0% /usr/bin/swupdate -f /etc/swupdate/swupdate.cfg -L -e rootfs,rootfs-1 -u
  246   243 root     S    19192   4%   0% /usr/libexec/pulse/gsettings-helper
  269   264 root     S    14480   3%   0% /usr/bin/swupdate -f /etc/swupdate/swupdate.cfg -L -e rootfs,rootfs-1 -u
  184     1 root     S    11324   2%   0% /sbin/udevd -d
  238     1 root     S     7900   2%   0% /usr/sbin/ntpd -g
32761   254 root     S     6304   1%   0% sshd: root@pts/0
13393   254 root     S     6304   1%   0% sshd: root@pts/1
  305   254 root     S     6000   1%   0% sshd: root@notty
13402   254 root     S     6000   1%   0% sshd: root@notty
  254     1 root     S     5880   1%   0% /usr/sbin/sshd
  323   305 root     S     5344   1%   0% /usr/libexec/sftp-server
13416 13402 root     S     5344   1%   0% /usr/libexec/sftp-server

```
> 可见,AI模型和相关的图像处理开销极大,没有 NPU / 硬件加速 → 纯 CPU 硬扛 AI
> 这可能已经是这块板子的性能极限了！
> 但是,幸好我还藏了一手---降转化质量TJSAMP_420+20%
> 降分辨率160x120 /128x96,推理模型也许支持但是效果极差

