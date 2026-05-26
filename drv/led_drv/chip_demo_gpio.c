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

#include "led_opr.h"
#include "leddrv.h"
#include "led_resource.h"

#include <linux/io.h>  // 新增：用于ioremap寄存器操作
static int g_ledpins[100];
static int g_ledcnt = 0;

// 定义IMX6ULL寄存器基地址（你的裸驱动地址）
#define CCM_CCGR1_BASE 0x020C406C  // GPIO5时钟寄存器
#define IOMUXC_SNVS_TAMPER3 0x02290014  // GPIO5_IO3复用寄存器
#define GPIO5_BASE 0x020AC000  // GPIO5基地址
#define GPIO5_DR (GPIO5_BASE + 0x00)    // 数据寄存器
#define GPIO5_GDIR (GPIO5_BASE + 0x04)  // 方向寄存器

static int board_demo_led_init (int which) /* 初始化LED, which-哪个LED */       
{   
    printk("init gpio: group %d, pin %d\n", GROUP(g_ledpins[which]), PIN(g_ledpins[which]));
    
    int group = GROUP(g_ledpins[which]);
    int pin = PIN(g_ledpins[which]);
    
    // 只处理你的GPIO5_IO3（其他组可后续扩展）
    if (group == 5 && pin == 3)
    {
        // 1. 开启GPIO5时钟（IMX6ULL必须操作，否则寄存器无效）
        volatile unsigned int *ccm_ccgr1 = ioremap(CCM_CCGR1_BASE, 4);
        *ccm_ccgr1 |= (1 << 3);  // 开启GPIO5时钟
        iounmap(ccm_ccgr1);
        
        // 2. 配置IOMUX：把SNVS_TAMPER3设置为GPIO模式（模式5）
        volatile unsigned int *iomux_reg = ioremap(IOMUXC_SNVS_TAMPER3, 4);
        *iomux_reg &= ~0xF;  // 清除原有模式
        *iomux_reg |= 0x5;   // 设置为GPIO模式
        iounmap(iomux_reg);
        
        // 3. 配置GPIO5_IO3为输出模式
        volatile unsigned int *gpio5_gdir = ioremap(GPIO5_GDIR, 4);
        *gpio5_gdir |= (1 << 3);  // 方向寄存器置1：输出模式
        iounmap(gpio5_gdir);
        
        // 4. 默认熄灭LED（输出高电平）
        volatile unsigned int *gpio5_dr = ioremap(GPIO5_DR, 4);
        *gpio5_dr |= (1 << 3);  // 数据寄存器置1：高电平，LED灭
        iounmap(gpio5_dr);
        
        printk("GPIO5_IO3初始化完成：时钟开启、模式配置、输出默认熄灭\n");
    }
    
    return 0;
}

static int board_demo_led_ctl (int which, char status) /* 控制LED, which-哪个LED, status:1-亮,0-灭 */
{
    printk("set led %s: group %d, pin %d\n", status ? "on" : "off", GROUP(g_ledpins[which]), PIN(g_ledpins[which]));

    int group = GROUP(g_ledpins[which]);
    int pin = PIN(g_ledpins[which]);
    
    if (group == 5 && pin == 3)
    {
        volatile unsigned int *gpio5_dr = ioremap(GPIO5_DR, 4);
        if (status == 1) {
            // 亮：输出低电平，清0 bit3
            *gpio5_dr &= ~(1 << 3);
        } else {
            // 灭：输出高电平，置1 bit3
            *gpio5_dr |= (1 << 3);
        }
        iounmap(gpio5_dr);
    }

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

static int chip_demo_gpio_probe(struct platform_device *pdev)
{
    struct resource *res;
    int i = 0;

    while (1)
    {
        res = platform_get_resource(pdev, IORESOURCE_IRQ, i++);
        if (!res)
            break;
        
        g_ledpins[g_ledcnt] = res->start;
        led_class_create_device(g_ledcnt);
        g_ledcnt++;
    }
    return 0;
    
}

static int chip_demo_gpio_remove(struct platform_device *pdev)
{
    struct resource *res;
    int i = 0;

    while (1)
    {
        res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
        if (!res)
            break;
        
        led_class_destroy_device(i);
        i++;
        g_ledcnt--;
    }
    return 0;
}


static struct platform_driver chip_demo_gpio_driver = {
    .probe      = chip_demo_gpio_probe,
    .remove     = chip_demo_gpio_remove,
    .driver     = {
        .name   = "100ask_led",
    },
};

static int __init chip_demo_gpio_drv_init(void)
{
    int err;
    
    err = platform_driver_register(&chip_demo_gpio_driver); 
    register_led_operations(&board_demo_led_opr);
    
    return 0;
}

static void __exit lchip_demo_gpio_drv_exit(void)
{
    platform_driver_unregister(&chip_demo_gpio_driver);
}

module_init(chip_demo_gpio_drv_init);
module_exit(lchip_demo_gpio_drv_exit);

MODULE_LICENSE("GPL");

