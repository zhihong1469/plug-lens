# 100ask IMX6ULL 专用 LED 驱动 Makefile
# 仅编译当前目录文件，不影响其他代码
KERN_DIR = /home/luo/linux/6ull/100ask_imx6ull-sdk/Linux-4.9.88

# 编译内核模块 + 应用测试程序
all:
	make -C $(KERN_DIR) M=`pwd` modules 
	$(CROSS_COMPILE)gcc -o ledtest ledtest.c -Wall

# 清理编译产物
clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order
	rm -f ledtest

# ===================== 核心修改：编译3个驱动文件 =====================
# 分层驱动3个文件：框架层 + 芯片层 + 板级层
obj-m += leddrv.o
obj-m += chip_demo_gpio.o
obj-m += board_A_led.o
# ====================================================================