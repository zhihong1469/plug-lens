#include <linux/module.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>  // 寄存器映射

#include "led_opr.h"
#include "leddrv.h"
#include "led_resource.h"

/* 全局变量：保存引脚 + 虚拟地址（只映射一次） */
static int g_ledpins[100];
static int g_ledcnt = 0;

/* IMX6ULL 寄存器物理地址 */
#define CCM_CCGR1_BASE          0x020C406C
#define IOMUXC_SNVS_TAMPER3     0x02290014
#define GPIO5_BASE              0x020AC000
#define GPIO5_DR                0x00
#define GPIO5_GDIR              0x04

/* 全局虚拟地址（PROBE映射，REMOVE卸载） */
static void __iomem *ccm_ccgr1_addr;
static void __iomem *iomux_addr;
static void __iomem *gpio5_gdir_addr;
static void __iomem *gpio5_dr_addr;

/* 硬件初始化：一次性映射寄存器，只执行1次 */
static int imx6ull_led_hw_init(void)
{
    // 1. 映射所有寄存器（一次性完成）
    ccm_ccgr1_addr = ioremap(CCM_CCGR1_BASE, 4);
    iomux_addr = ioremap(IOMUXC_SNVS_TAMPER3, 4);
    gpio5_gdir_addr = ioremap(GPIO5_BASE + GPIO5_GDIR, 4);
    gpio5_dr_addr = ioremap(GPIO5_BASE + GPIO5_DR, 4);

    if (!ccm_ccgr1_addr || !iomux_addr || !gpio5_gdir_addr || !gpio5_dr_addr) {
        printk(KERN_ERR "GPIO ioremap failed!\n");
        return -ENOMEM;
    }

    // 2. 开启GPIO5时钟（IMX6ULL 2位配置：CG6 = bit6-7 → 0b11）
    writel(readl(ccm_ccgr1_addr) | (3 << 6), ccm_ccgr1_addr);

    // 3. 配置引脚为GPIO模式（模式5）
    writel(0x5, iomux_addr);

    // 4. 设置为输出模式
    writel(readl(gpio5_gdir_addr) | (1 << 3), gpio5_gdir_addr);

    // 5. 默认熄灭LED
    writel(readl(gpio5_dr_addr) | (1 << 3), gpio5_dr_addr);

    printk(KERN_INFO "IMX6ULL GPIO5_IO3 init success\n");
    return 0;
}

/* 硬件释放：一次性取消映射 */
static void imx6ull_led_hw_exit(void)
{
    // 熄灭LED
    if (gpio5_dr_addr)
        writel(readl(gpio5_dr_addr) | (1 << 3), gpio5_dr_addr);

    // 取消地址映射
    iounmap(ccm_ccgr1_addr);
    iounmap(iomux_addr);
    iounmap(gpio5_gdir_addr);
    iounmap(gpio5_dr_addr);
}

static int board_demo_led_init (int which)
{
    int group = GROUP(g_ledpins[which]);
    int pin = PIN(g_ledpins[which]);

    printk(KERN_INFO "init led: group=%d, pin=%d\n", group, pin);

    // 硬件已在probe初始化，这里无需重复操作
    return 0;
}

static int board_demo_led_ctl (int which, char status)
{
    int group = GROUP(g_ledpins[which]);
    int pin = PIN(g_ledpins[which]);

    if (group == 5 && pin == 3) {
        if (status == 1) {
            // 亮：低电平
            writel(readl(gpio5_dr_addr) & ~(1 << 3), gpio5_dr_addr);
        } else {
            // 灭：高电平
            writel(readl(gpio5_dr_addr) | (1 << 3), gpio5_dr_addr);
        }
    }

    printk(KERN_INFO "set led %s: group=%d, pin=%d\n",
           status ? "on" : "off", group, pin);
    return 0;
}

static struct led_operations board_demo_led_opr = {
    .init = board_demo_led_init,
    .ctl  = board_demo_led_ctl,
};

struct led_operations *get_board_led_opr(void)
{
    return &board_demo_led_opr;
}

/* ======================================
   PROBE：完美兼容 设备树 + 传统总线模型
====================================== */
static int chip_demo_gpio_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    int led_pin = 0;
    int err;

    /* 1. 初始化硬件（只执行一次） */
    static int hw_inited = 0;
    if (!hw_inited) {
        err = imx6ull_led_hw_init();
        if (err) return err;
        hw_inited = 1;
    }

    /* 2. 读取引脚：双模型兼容 */
    if (np) {
        // 设备树
        err = of_property_read_u32(np, "pin", &led_pin);
    } else {
        // 传统platform总线
        struct resource *res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
        if (!res) return -ENODEV;
        led_pin = res->start;
    }

    /* 3. 注册LED设备 */
    g_ledpins[g_ledcnt] = led_pin;
    led_class_create_device(g_ledcnt);
    g_ledcnt++;

    return 0;
}

/* ======================================
   REMOVE：修复！兼容双模型 + 释放资源
====================================== */
static int chip_demo_gpio_remove(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    int led_pin = 0;
    int i, err;

    /* 读取引脚：双模型兼容 */
    if (np) {
        err = of_property_read_u32(np, "pin", &led_pin);
    } else {
        struct resource *res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
        if (!res) return -ENODEV;
        led_pin = res->start;
    }

    /* 注销设备 */
    for (i = 0; i < g_ledcnt; i++) {
        if (g_ledpins[i] == led_pin) {
            led_class_destroy_device(i);
            g_ledpins[i] = -1;
            break;
        }
    }

    /* 重置计数 */
    for (i = 0; i < g_ledcnt; i++) {
        if (g_ledpins[i] != -1) break;
    }
    if (i == g_ledcnt) {
        g_ledcnt = 0;
        imx6ull_led_hw_exit(); // 无设备时释放硬件
    }

    return 0;
}

/* 设备树匹配表 */
static const struct of_device_id ask100_leds[] = {
    { .compatible = "100ask,leddrv" },
    { },
};

/* platform驱动结构体 */
static struct platform_driver chip_demo_gpio_driver = {
    .probe      = chip_demo_gpio_probe,
    .remove     = chip_demo_gpio_remove,
    .driver     = {
        .name   = "100ask_led",          // 传统总线匹配名
        .of_match_table = ask100_leds,   // 设备树匹配
    },
};

static int __init chip_demo_gpio_drv_init(void)
{
    int err;
    err = platform_driver_register(&chip_demo_gpio_driver);
    register_led_operations(&board_demo_led_opr);
    return 0;
}

static void __exit chip_demo_gpio_drv_exit(void)
{
    platform_driver_unregister(&chip_demo_gpio_driver);
}

module_init(chip_demo_gpio_drv_init);
module_exit(chip_demo_gpio_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("100ASK");
MODULE_DESCRIPTION("IMX6ULL LED DRIVER (DT + Platform)");