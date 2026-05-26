#include "led_controller.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief LED控制器私有结构体（仅内部可见）
 */
struct LedController {
    int         fd;             /* 设备文件描述符 */
    bool        is_open;        /* 设备打开状态 */
    bool        current_state;  /* 当前LED亮灭状态 */
    char        dev_path[64];   /* 设备节点路径 */
};

/**
 * @brief 内部私有函数：向设备写入状态
 */
static LedError_t led_write_state(LedController_t* self, uint8_t state)
{
    if (!self || !self->is_open) {
        return LED_ERROR_NOT_OPEN;
    }

    ssize_t ret = write(self->fd, &state, 1U);
    if (ret != 1) {
        return LED_ERROR_WRITE_DEV;
    }

    self->current_state = (state == 1U);
    return LED_OK;
}

/* ==================== 公共API实现 ==================== */
LedController_t* LedController_Create(void)
{
    LedController_t* self = (LedController_t*)malloc(sizeof(LedController_t));
    if (!self) {
        return NULL;
    }

    /* 初始化默认值 */
    self->fd = -1;
    self->is_open = false;
    self->current_state = false;
    memset(self->dev_path, 0, sizeof(self->dev_path));

    return self;
}

void LedController_Destroy(LedController_t* self)
{
    if (!self) {
        return;
    }

    /* 先关闭设备，再释放内存 */
    LedController_Close(self);
    free(self);
}

LedError_t LedController_Open(LedController_t* self, const char* dev_path)
{
    if (!self || !dev_path || (strlen(dev_path) >= sizeof(self->dev_path))) {
        return LED_ERROR_INVALID_PARAM;
    }

    /* 避免重复打开 */
    if (self->is_open) {
        return LED_OK;
    }

    int fd = open(dev_path, O_RDWR);
    if (fd == -1) {
        return LED_ERROR_OPEN_DEV;
    }

    /* 初始化实例参数 */
    self->fd = fd;
    self->is_open = true;
    strncpy(self->dev_path, dev_path, sizeof(self->dev_path) - 1U);

    /* 默认上电灭灯 */
    led_write_state(self, 0U);

    return LED_OK;
}

void LedController_Close(LedController_t* self)
{
    if (!self || !self->is_open) {
        return;
    }

    /* 灭灯后关闭设备 */
    led_write_state(self, 0U);
    close(self->fd);

    self->fd = -1;
    self->is_open = false;
    memset(self->dev_path, 0, sizeof(self->dev_path));
}

LedError_t LedController_TurnOn(LedController_t* self)
{
    return led_write_state(self, 1U);
}

LedError_t LedController_TurnOff(LedController_t* self)
{
    return led_write_state(self, 0U);
}

bool LedController_GetState(const LedController_t* self)
{
    if (!self) {
        return false;
    }
    return self->current_state;
}