#include "service_base.h"

/**
 * @brief  服务基类入参合法性校验
 * @param  self: 服务基类指针
 * @return 0-合法，-1-非法
 */
static inline int service_base_check_valid(service_base_t *self)
{
    // 校验指针和操作表非空
    if (!self || !self->ops) {
        return -1;
    }
    return 0;
}

int service_base_init(service_base_t *self)
{
    int ret;

    // 入参校验
    if (service_base_check_valid(self) != 0) {
        return -1;
    }

    // 状态校验：仅空闲状态可初始化
    if (self->state != SRV_STATE_IDLE) {
        return -2;
    }

    // 调用子类初始化实现
    ret = self->ops->init(self);
    if (ret == 0) {
        self->state = SRV_STATE_INIT;
    } else {
        self->state = SRV_STATE_ERROR;
    }

    return ret;
}

int service_base_start(service_base_t *self)
{
    int ret;

    if (service_base_check_valid(self) != 0) {
        return -1;
    }

    // 状态校验：仅初始化状态可启动
    if (self->state != SRV_STATE_INIT) {
        return -2;
    }

    ret = self->ops->start(self);
    if (ret == 0) {
        self->state = SRV_STATE_RUNNING;
    } else {
        self->state = SRV_STATE_ERROR;
    }

    return ret;
}

int service_base_pause(service_base_t *self)
{
    int ret;

    if (service_base_check_valid(self) != 0) {
        return -1;
    }

    // 状态校验：仅运行状态可暂停
    if (self->state != SRV_STATE_RUNNING) {
        return -2;
    }

    ret = self->ops->pause(self);
    if (ret == 0) {
        self->state = SRV_STATE_PAUSE;
    }

    return ret;
}

int service_base_resume(service_base_t *self)
{
    int ret;

    if (service_base_check_valid(self) != 0) {
        return -1;
    }

    // 状态校验：仅暂停状态可恢复
    if (self->state != SRV_STATE_PAUSE) {
        return -2;
    }

    ret = self->ops->resume(self);
    if (ret == 0) {
        self->state = SRV_STATE_RUNNING;
    }

    return ret;
}

int service_base_stop(service_base_t *self)
{
    int ret;

    if (service_base_check_valid(self) != 0) {
        return -1;
    }

    // 状态校验：运行/暂停状态可停止
    if (self->state != SRV_STATE_RUNNING && self->state != SRV_STATE_PAUSE) {
        return -2;
    }

    ret = self->ops->stop(self);
    if (ret == 0) {
        self->state = SRV_STATE_STOP;
    } else {
        self->state = SRV_STATE_ERROR;
    }

    return ret;
}

int service_base_deinit(service_base_t *self)
{
    int ret;

    if (service_base_check_valid(self) != 0) {
        return -1;
    }

    // 状态校验：仅停止状态可销毁
    if (self->state != SRV_STATE_STOP) {
        return -2;
    }

    ret = self->ops->deinit(self);
    if (ret == 0) {
        self->state = SRV_STATE_IDLE;
    }

    return ret;
}

void service_base_event_handle(service_base_t *self, uint32_t event_id, void *data)
{
    if (service_base_check_valid(self) != 0) {
        return;
    }

    // 转发事件至子类处理
    self->ops->event_handle(self, event_id, data);
}

srv_state_t service_base_get_state(service_base_t *self)
{
    if (!self) {
        return SRV_STATE_ERROR;
    }
    return self->state;
}