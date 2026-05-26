#include "led_indicator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/**
 * @brief LED指示灯私有结构体
 * 规范: 基类必须放在第一个成员
 */
typedef struct {
    led_base_t      base;       // LED基类(必须第一个)
    const char     *dev_path;   // 设备路径 /dev/100ask_led0
    int             fd;         // 设备文件描述符
} led_indicator_t;

/* ============================================================================
 * 私有工具函数
 * ========================================================================== */
static int led_indicator_write(led_indicator_t *me, uint8_t state)
{
    if (me->fd < 0) {
        return -ENODEV;
    }

    ssize_t ret = write(me->fd, &state, 1);
    if (ret != 1) {
        perror("[LED IND] write failed");
        return -EIO;
    }
    return 0;
}

/* ============================================================================
 * 子类虚函数实现(继承 led_base)
 * ========================================================================== */
/**
 * @brief 初始化: 打开设备, 默认灭灯
 */
static int led_indicator_init(led_base_t *base)
{
    led_indicator_t *me = (led_indicator_t *)base;

    // 打开LED设备
    me->fd = open(me->dev_path, O_RDWR);
    if (me->fd < 0) {
        perror("[LED IND] open failed");
        return -errno;
    }

    // 默认灭灯
    int ret = led_indicator_write(me, 0);
    if (ret < 0) {
        close(me->fd);
        me->fd = -1;
        return ret;
    }

    printf("[LED IND] 初始化完成 ✅\n");
    return 0;
}

/**
 * @brief 反初始化: 灭灯 + 关闭设备
 */
static int led_indicator_deinit(led_base_t *base)
{
    led_indicator_t *me = (led_indicator_t *)base;

    if (me->fd >= 0) {
        led_indicator_write(me, 0);
        close(me->fd);
        me->fd = -1;
    }

    printf("[LED IND] 反初始化完成\n");
    return 0;
}

/**
 * @brief 设置LED状态(亮/灭/闪烁)
 * 本驱动仅支持 亮/灭
 */
static int led_indicator_set_state(led_base_t *base, led_mode_t mode)
{
    led_indicator_t *me = (led_indicator_t *)base;
    uint8_t val = 0;

    switch (mode) {
        case LED_MODE_ON:
            val = 1;
            break;
        case LED_MODE_OFF:
            val = 0;
            break;
        case LED_MODE_BLINK:
            // 如需闪烁,可在此扩展定时器
            printf("[LED IND] 闪烁模式暂不支持\n");
            return -ENOTSUP;
        default:
            return -EINVAL;
    }

    return led_indicator_write(me, val);
}

/* ============================================================================
 * LED子类虚函数表(必须const)
 * ========================================================================== */
static const led_ops_t g_led_indicator_ops = {
    .init      = led_indicator_init,
    .deinit    = led_indicator_deinit,
    .set_state = led_indicator_set_state,
    .get_state = NULL,   // 无需实现
};

/* ============================================================================
 * 对外构造/析构函数
 * ========================================================================== */
led_base_t *led_indicator_create(const char *dev_path)
{
    if (!dev_path) {
        return NULL;
    }

    // 分配内存
    led_indicator_t *me = (led_indicator_t *)calloc(1, sizeof(*me));
    if (!me) {
        printf("[LED IND] 内存分配失败\n");
        return NULL;
    }

    // 基类初始化
    me->base.ops   = &g_led_indicator_ops;
    me->base.name  = "led_indicator";
    me->base.state = LED_STATE_IDLE;
    me->base.current_mode = LED_MODE_OFF;

    // 私有参数
    me->dev_path = dev_path;
    me->fd       = -1;

    printf("[LED IND] 创建成功\n");
    return &me->base;
}

void led_indicator_destroy(led_base_t *base)
{
    if (!base) {
        return;
    }

    led_indicator_t *me = (led_indicator_t *)base;

    // 资源释放
    led_base_deinit(base);
    free(me);

    printf("[LED IND] 销毁成功\n");
}