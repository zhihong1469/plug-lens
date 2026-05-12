/* SPDX-License-Identifier: MIT */
/**
 * @file initcall.c
 * @brief 自动初始化机制运行时实现
 * @details 遍历链接器合并后的初始化段，执行所有模块初始化函数
 * @note 与业务完全解耦，纯框架底层代码
 */

#include "initcall.h"
#include "log.h"

/**
 * @brief 执行所有模块初始化
 * @return 无
 */
void do_initcalls(void)
{
    initcall_t *fn;
    int32_t init_count = 0;

    LOG_I("[InitCall] 开始自动初始化，段地址: [%p, %p]",
          (void *)__start_my_initcall, (void *)__stop_my_initcall);

    /**
     * 遍历初始化段：
     * 从段起始地址 -> 段结束地址，逐个执行初始化函数
     */
    for (fn = __start_my_initcall; fn < __stop_my_initcall; fn++)
    {
        int ret = (*fn)();
        init_count++;

        if (ret == 0)
        {
            LOG_I("[InitCall] 模块初始化成功: 函数地址=%p", (void *)*fn);
        }
        else
        {
            LOG_E("[InitCall] 模块初始化失败: 函数地址=%p, ret=%d", (void *)*fn, ret);
        }
    }

    LOG_I("[InitCall] 自动初始化完成，总模块数: %d", init_count);
}
