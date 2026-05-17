/* SPDX-License-Identifier: MIT */
/**
 * @file        initcall.h
 * @brief       Linux插件层自动初始化框架（带优先级顺序控制）
 * @details     基于GCC自定义段实现，解决插件自动加载/初始化顺序控制问题
 *              核心修复：通过双重宏展开解决预处理器嵌套展开失效问题
 * @author      Luozhihong
 * @date        2026
 * @note        所有自动初始化模块均来自插件层(libplug.a)
 */

#ifndef __INITCALL_H__
#define __INITCALL_H__

#include <stdint.h>

/**
 * @brief 初始化函数指针类型定义
 * @param 无
 * @return int32_t 0=初始化成功，负数=初始化失败
 * @note 所有插件初始化函数必须遵循该函数签名
 */
typedef int (*initcall_t)(void);

// ====================== 优先级定义（不变） ======================
// 数字越小，初始化优先级越高，执行顺序越靠前(数字后面禁止加任何字符)
#define INIT_SYS        0
#define INIT_BUS        1
#define INIT_DEVICE     2
#define INIT_SERVICE    3
#define INIT_APP        4


/************************** 宏展开修复核心 **************************/
/**
 * @brief 一级字符串化宏（内部使用）
 * @param x 待展开的宏
 * @note 解决GCC预处理器嵌套宏无法展开的问题
 */
#define _STRINGIFY(x) #x

/**
 * @brief 二级字符串化宏（对外使用）
 * @param x 待展开的宏
 * @details 双重展开：先展开level宏，再字符串化，生成正确的段名
 *          修复：直接使用#level导致宏不展开，段名匹配失败
 */
#define STRINGIFY(x) _STRINGIFY(x)

/************************** 核心初始化注册宏 **************************/
/**
 * @brief 插件初始化注册通用宏（核心接口）
 * @param level 初始化优先级(INIT_SYS/INIT_BUS/INIT_DEVICE/INIT_SERVICE/INIT_APP)
 * @param fn    插件初始化函数名（无参数，返回int）
 * @details 将初始化函数指针放入指定优先级的自定义段中
 *          used：禁止编译器优化删除未显式调用的函数
 *          section：指定GCC自定义段，实现按优先级分类存储
 * @note 每个插件仅需调用一次该宏，即可实现自动注册
 */
#define MODULE_INIT_LEVEL(level, fn) \
    static initcall_t __initcall_##fn \
    __attribute__((used, section(".my_initcall_" STRINGIFY(level)))) = fn

/************************** 便捷调用宏（简化使用） **************************/
#define MODULE_INIT_SYS(fn)      MODULE_INIT_LEVEL(INIT_SYS, fn)      /*!< 系统级便捷注册 */
#define MODULE_INIT_BUS(fn)      MODULE_INIT_LEVEL(INIT_BUS, fn)      /*!< 总线级便捷注册 */
#define MODULE_INIT_DEVICE(fn)   MODULE_INIT_LEVEL(INIT_DEVICE, fn)   /*!< 设备级便捷注册 */
#define MODULE_INIT_SERVICE(fn)  MODULE_INIT_LEVEL(INIT_SERVICE, fn)  /*!< 服务级便捷注册 */
#define MODULE_INIT_APP(fn)      MODULE_INIT_LEVEL(INIT_APP, fn)      /*!< 应用级便捷注册 */

/************************** 自定义段边界声明 **************************/
/**
 * @brief 批量声明自定义段的起始/结束地址
 * @param level 优先级等级
 * @details 链接脚本中定义的段边界符号，用于遍历执行初始化函数
 */
#define DECLARE_INITCALL_SEG(level) \
    extern initcall_t __start_my_initcall_##level[]; \
    extern initcall_t __stop_my_initcall_##level[];

// 批量声明所有优先级的段边界
DECLARE_INITCALL_SEG(0);
DECLARE_INITCALL_SEG(1);
DECLARE_INITCALL_SEG(2);
DECLARE_INITCALL_SEG(3);
DECLARE_INITCALL_SEG(4);

// 取消内部辅助宏，防止命名污染
#undef DECLARE_INITCALL_SEG

/************************** 对外接口函数 **************************/
/**
 * @brief 执行所有已注册的插件初始化（按优先级顺序）
 * @param 无
 * @return 无
 * @details 遍历所有优先级自定义段，按 0→1→2→3→4 顺序执行初始化函数
 * @note 仅需在main函数中调用一次
 */
void do_initcalls(void);

#endif /* __INITCALL_H__ */