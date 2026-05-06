# 快速上手（示例或长命令便携）
编译（环境自行配置）：
 make clean && make
传输到开发板（示例）：
cp output/vision_ai_app ~/nfs/run_on_board/
[root@100ask:/mnt/run_on_board]# export LD_LIBRARY_PATH=/mnt/run_on_board:$LD_LIBRARY_PATH
[root@100ask:/mnt/run_on_board]# export LD_LIBRARY_PATH=/mnt/run_on_board/install_arm/lib/:$Ld_LIBRARY_PATH

GDB调试：
arm-buildroot-linux-gnueabihf-gdb ./output/vision_ai_app
[root@100ask:/mnt/run_on_board]# ./gdbserver --once :12345 ./vision_ai_app
  target remote 192.168.5.9:12345
  thread apply all bt

# 结构速通（详细见documents/architecture.md）
一般应用层只需要专注plugins目录下的代码修改，src实现接口函数或者架构相关
ex:
新增功能需要实现HAL层代码，xxx_hal.c的原代码也放plugins目录下，xxx_hal.h放入src目录
## 注意
    六层架构调用顺序（从上到下）
    应用层 → 服务层 → 数据链路层 → 硬件抽象层 → 插件层 → 硬件
    编译顺序铁律
    common（公共库） → plugins（硬件实现） → src（核心框架）

# 项目开发细节（详细见documents/details.md）

# 工程目录可选项
## 文档利用（可选/documents）
documents/cur_issue.md 可用于记录当前代码开发流程或问题，或者方便问题或工作被快速交接（以往所有git提交记录均可查）
documents/debug.txt 用于拷贝记录开发板或编译调式问题日志

## 脚本（可选/scripts）
批量打印当前工程代码到指定文本，方便遇到bug或审查低级错误——快速利用AI工具寻找灵感。
scripts/print_core_src.sh 
scripts/print_all_src.sh
GDB配置脚本
scripts/.gdbinit 
- 使用方法(自选)：
方案 1：临时加载（每次启动 GDB 指定）
```bash
gdb -x scripts/.gdbinit 你的程序
```
方案 2：自动加载（一劳永逸，推荐）
```bash
# 编辑 ~/.gdbinit，添加这行（路径写你实际的绝对/相对路径）
source ~/你的项目路径/scripts/.gdbinit
```
## .tool（外部开源工具集合）
ex：.tool/gdb-12.1.tar.xz 用于开发板资源较小无预装GDB，可考虑静态链接gdbserver

##