/* SPDX-License-Identifier: MIT */
/**
 * @file        initcall.c
 * @brief       自动初始化框架实现文件
 * @details     按优先级遍历自定义段，执行所有插件初始化函数
 * @note        无业务耦合，纯框架底层实现
 */

#include "initcall.h"
#include "log.h"

/**
 * @def DO_INIT_LEVEL
 * @brief 单优先级段遍历执行宏（消除重复代码）
 * @param level 优先级等级
 * @details 遍历对应优先级的自定义段，执行所有初始化函数
 *          自动统计初始化成功的模块数量
 */
#define DO_INIT_LEVEL(level) do { \
    initcall_t *fn; \
    for (fn = __start_my_initcall_##level; fn < __stop_my_initcall_##level; fn++) { \
        (*fn)();                /* 执行插件初始化函数 */ \
        init_count++;           /* 统计初始化模块数量 */ \
        LOG_I("[InitCall] 优先级%d 模块初始化成功", level); \
    } \
} while(0)

/**
 * @brief 按优先级执行所有插件初始化
 * @param 无
 * @return 无
 * @details 执行顺序严格固定：
 *          系统级 → 总线级 → 设备级 → 服务级 → 应用级
 */
void do_initcalls(void)
{
    // 统计初始化完成的模块总数
    int32_t init_count = 0;

    LOG_I("[InitCall] 按优先级自动初始化开始");

    // 严格按照优先级顺序执行（不可调换顺序）
    DO_INIT_LEVEL(0);  /* 系统级初始化 */
    DO_INIT_LEVEL(1);  /* 总线级初始化 */
    DO_INIT_LEVEL(2);  /* 设备级初始化（采集模块） */
    DO_INIT_LEVEL(3);  /* 服务级初始化（人脸模块） */
    DO_INIT_LEVEL(4);  /* 应用级初始化 */

    LOG_I("[InitCall] 自动初始化完成，总模块数: %d", init_count);
}

// 取消内部辅助宏
#undef DO_INIT_LEVEL