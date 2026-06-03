# ===================== 基础配置 =====================
KERN_DIR := /home/luo/linux/6ull/100ask_imx6ull-sdk/Linux-4.9.88
PWD     := $(shell pwd)
CC      := $(CROSS_COMPILE)gcc
DTC     := $(KERN_DIR)/scripts/dtc/dtc

# 正确写法：小写 -i 指定包含路径！！！
DTC_INC := -i $(KERN_DIR)/include -i $(KERN_DIR)/arch/arm/boot/dts

# ===================== 驱动配置 =====================
obj-m += leddrv.o
obj-m += chip_demo_gpio.o
obj-m += board_A_led.o

# ===================== 设备树 =====================
DTS_SRC := dts/100ask_led.dts
DTB_OUT := 100ask_led.dtb

# ===================== 测试应用 =====================
TEST_APP := ledtest

all: module app dtb

module:
	@make -C $(KERN_DIR) M=$(PWD) modules

# 修复完成！标准企业编译命令
dtb:
	@echo "编译设备树: $(DTS_SRC)"
	$(DTC) $(DTC_INC) -I dts -O dtb -o $(DTB_OUT) $(DTS_SRC)

app:
	$(CC) -o $(TEST_APP) $(TEST_APP).c -Wall

clean:
	@make -C $(KERN_DIR) M=$(PWD) modules clean
	rm -rf modules.order Module.symvers
	rm -f $(TEST_APP) *.dtb *.ko *.o *.mod.c