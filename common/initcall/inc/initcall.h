/* SPDX-License-Identifier: MIT */
/**
 * @file initcall.h
 * @brief 仿Linux内核自动初始化机制（段式注册）
 * @details 实现模块自动注册/初始化，main函数无需手动调用业务初始化
 *          核心：编译器段属性 + 链接器合并段 + 运行时遍历执行
 * @note 遵循开闭原则：对扩展开放，对修改关闭
 * @version 1.0
 * @date 2025
 */

#ifndef __INITCALL_H__
#define __INITCALL_H__

#include <stdint.h>

/**
 * @brief 初始化函数指针类型定义
 * @return 0=成功，负数=失败
 * @note 所有业务模块初始化函数必须遵循该签名
 */
typedef int (*initcall_t)(void);

/**
 * @def MODULE_INIT
 * @brief 模块初始化注册宏（核心）
 * @param fn 模块初始化函数名
 * @details 将初始化函数指针放入自定义段 my_initcall
 *          __attribute__((used))：防止编译器优化删除未显式调用的函数
 *          __attribute__((section("my_initcall")))：指定数据段
 * @note 每个业务模块仅需调用一次该宏，即可自动注册初始化
 */
#define MODULE_INIT(fn)                                    \
    static initcall_t __initcall_##fn                     \
    __attribute__((used, section(".my_initcall"))) = fn

/**
 * @brief 链接器自动生成的段边界符号
 * @details __start_my_initcall：初始化段起始地址
 *          __stop_my_initcall：初始化段结束地址
 * @note 声明为数组，方便直接做地址遍历/比较
 */
extern initcall_t __start_my_initcall[];
extern initcall_t __stop_my_initcall[];

/**
 * @brief 执行所有已注册的模块初始化
 * @return 无
 * @details 遍历 my_initcall 段，执行所有注册的初始化函数
 *          由 main.c 统一调用，无需手动管理模块
 */
void do_initcalls(void);

#endif /* __INITCALL_H__ */