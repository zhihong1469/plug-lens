/* SPDX-License-Identifier: MIT */
/**
 * @file    daemon.h
 * @brief   Standard Linux daemon process management interface
 *          标准Linux守护进程管理接口
 * @details 模块核心能力：
 *          1. 实现嵌入式Linux标准双fork守护进程创建逻辑
 *          2. 脱离终端控制，后台静默运行，支持产品级部署
 *          3. 适配plug-lens视觉AI终端后台服务托管
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 *
 * @note    全局使用规范：
 *          1. 必须在程序初始化最早期调用，优先于所有业务逻辑
 *          2. 仅支持产品模式(RUN_PRODUCT_MODE=1)下调用
 *          3. 调用后标准输入/输出/错误重定向至/dev/null
 */
#ifndef DAEMON_H
#define DAEMON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Create standard Linux daemon process
 *          创建标准Linux守护进程（双fork模式）
 * @return  0 on success, -1 on failure
 *          返回值：0=创建成功，-1=创建失败（系统调用错误）
 *
 * @pre     前置条件：
 *          1. 程序处于初始化阶段，未启动任何业务线程/服务
 *          2. 产品模式(RUN_PRODUCT_MODE=1)使能，调试模式禁止调用
 *          3. 进程拥有足够的系统权限创建子进程
 *
 * @post    后置条件：
 *          1. 父进程退出，子进程成为1号进程子节点
 *          2. 脱离控制终端，无标准输入输出
 *          3. 工作目录切换至根目录/指定目录
 *          4. 文件掩码重置，关闭无用文件描述符
 *
 * @note    注意事项：
 *          1. 采用工业级标准双fork机制，避免获取控制终端
 *          2. 自动重定向stdin/stdout/stderr到/dev/null
 *          3. 守护进程生命周期与系统同步，支持开机自启
 *
 * @warning 警告信息：
 *          - 禁止在调试模式、线程上下文、信号处理函数中调用
 *          - 调用后禁止使用printf/scanf等终端IO函数
 *          - 失败后需立即退出程序，禁止继续执行业务逻辑
 *
 * @thread_safety 线程安全：否
 *                单例初始化接口，仅允许主线程在启动时调用一次
 *
 * @example 使用示例：
 * @code
 * // 产品模式下初始化守护进程
 * #if RUN_PRODUCT_MODE
 * if (create_daemon() != 0) {
 *     // 守护进程创建失败，退出程序
 *     return -1;
 * }
 * #endif
 *
 * // 后续启动业务逻辑（视觉采集、AI推理、推流服务）
 * @endcode
 */
int create_daemon(void);

#ifdef __cplusplus
}
#endif

#endif // DAEMON_H