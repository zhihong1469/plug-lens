# ==========================================================================
# 嵌入式 Linux GDB 调试配置
# ==========================================================================

# 1. 信号处理配置（核心！解决 Ctrl+C 被拦截问题）
handle SIGINT  pass nostop noprint  # Ctrl+C：直接传给程序
handle SIGTERM pass nostop noprint  # kill 命令：直接传给程序
handle SIGSEGV stop print pass      # 段错误：暂停并打印
handle SIGABRT stop print pass      # abort：暂停并打印

# 2. 输出配置
set confirm off              # 关闭确认提示（如 quit 时）
set pagination off           # 关闭分页（避免 ---Type <return> to continue---）
set print elements 0         # 打印数组时不限制长度
set print pretty on          # 结构体美化打印
set verbose off              # 减少冗余输出

# 3. 历史记录
set history save on
set history size 1000

# 4. 提示信息
echo ========================================\n
echo  嵌入式 Linux GDB 调试环境已配置\n
echo  常用命令：\n
echo    c    - 继续运行\n
echo    n    - 单步跳过\n
echo    s    - 单步进入\n
echo    bt   - 查看调用栈\n
echo    info threads - 查看所有线程\n
echo ========================================\n