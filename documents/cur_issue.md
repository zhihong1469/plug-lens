## 问题（认知型）
- GDB 默认拦截 SIGINT (Ctrl+C) 用于中断调试，导致信号无法送达应用层
> 配置 GDB 放行信号后，_signal_handler 断点正常触发，程序优雅退出

新增GDB管理脚本
