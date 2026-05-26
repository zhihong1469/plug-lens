#ifndef __SERVICE_BASE_H
#define __SERVICE_BASE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 服务统一状态枚举
 * @note 与项目 module_fsm 状态机完全对齐，所有服务共用
 */
typedef enum {
    SRV_STATE_IDLE    = 0,  /**< 空闲状态   ：服务未初始化/已销毁     */
    SRV_STATE_INIT    = 1,  /**< 初始化状态 ：资源已分配，未启动运行   */
    SRV_STATE_RUNNING = 2,  /**< 运行状态   ：服务线程正常工作中       */
    SRV_STATE_PAUSE   = 3,  /**< 暂停状态   ：服务临时挂起，可恢复     */
    SRV_STATE_STOP    = 4,  /**< 停止状态   ：服务线程已退出，未销毁   */
    SRV_STATE_ERROR   = 5,  /**< 异常状态   ：服务执行失败/故障        */
} srv_state_t;

/**
 * @brief 服务多态操作表
 * @note 强制7个标准接口，所有子类必须完整实现，不可增删
 */
typedef struct service_ops {
    /**
     * @brief  服务初始化接口
     * @param  self: 子类服务实例指针
     * @return 0-成功，负数-失败
     */
    int  (*init)(void *self);

    /**
     * @brief  服务启动接口
     * @param  self: 子类服务实例指针
     * @return 0-成功，负数-失败
     */
    int  (*start)(void *self);

    /**
     * @brief  服务暂停接口
     * @param  self: 子类服务实例指针
     * @return 0-成功，负数-失败
     */
    int  (*pause)(void *self);

    /**
     * @brief  服务恢复接口
     * @param  self: 子类服务实例指针
     * @return 0-成功，负数-失败
     */
    int  (*resume)(void *self);

    /**
     * @brief  服务停止接口
     * @param  self: 子类服务实例指针
     * @return 0-成功，负数-失败
     */
    int  (*stop)(void *self);

    /**
     * @brief  服务销毁接口
     * @param  self: 子类服务实例指针
     * @return 0-成功，负数-失败
     */
    int  (*deinit)(void *self);

    /**
     * @brief  事件总线处理接口
     * @param  self: 子类服务实例指针
     * @param  event_id: 事件总线ID
     * @param  data: 事件携带参数指针
     */
    void (*event_handle)(void *self, uint32_t event_id, void *data);
} service_ops_t;

/**
 * @brief 服务基类结构体
 * @note 所有子类结构体**第一个成员必须是该结构体**，实现C-OOP继承
 */
typedef struct service_base {
    const service_ops_t *ops;       /**< 多态操作表指针（绑定子类实现） */
    srv_state_t          state;     /**< 服务当前状态（统一状态机） */
    const char          *name;      /**< 服务名称（用于调试/标识） */
} service_base_t;

/************************* 基类公共接口（应用层统一调用） *************************/
/**
 * @brief  服务初始化（状态流转：IDLE → INIT）
 * @param  self: 服务基类指针
 * @return 0-成功，负数-入参非法/状态错误/子类初始化失败
 */
int service_base_init(service_base_t *self);

/**
 * @brief  服务启动（状态流转：INIT → RUNNING）
 * @param  self: 服务基类指针
 * @return 0-成功，负数-入参非法/状态错误/子类启动失败
 */
int service_base_start(service_base_t *self);

/**
 * @brief  服务暂停（状态流转：RUNNING → PAUSE）
 * @param  self: 服务基类指针
 * @return 0-成功，负数-入参非法/状态错误/子类暂停失败
 */
int service_base_pause(service_base_t *self);

/**
 * @brief  服务恢复（状态流转：PAUSE → RUNNING）
 * @param  self: 服务基类指针
 * @return 0-成功，负数-入参非法/状态错误/子类恢复失败
 */
int service_base_resume(service_base_t *self);

/**
 * @brief  服务停止（状态流转：RUNNING/PAUSE → STOP）
 * @param  self: 服务基类指针
 * @return 0-成功，负数-入参非法/状态错误/子类停止失败
 */
int service_base_stop(service_base_t *self);

/**
 * @brief  服务销毁（状态流转：STOP → IDLE）
 * @param  self: 服务基类指针
 * @return 0-成功，负数-入参非法/状态错误/子类销毁失败
 */
int service_base_deinit(service_base_t *self);

/**
 * @brief  服务事件处理（转发事件至子类）
 * @param  self: 服务基类指针
 * @param  event_id: 事件ID
 * @param  data: 事件参数指针
 */
void service_base_event_handle(service_base_t *self, uint32_t event_id, void *data);

/**
 * @brief  获取服务当前状态
 * @param  self: 服务基类指针
 * @return 服务状态枚举值，入参非法返回SRV_STATE_ERROR
 */
srv_state_t service_base_get_state(service_base_t *self);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_BASE_H */