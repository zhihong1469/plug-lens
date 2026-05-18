/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           mem_adapter.h
 * @brief          跨平台线程安全内存管理适配中间层
 * @defgroup       MEM_ADAPTER
 * @details
 *  1. 双模式支持：TLSF高性能静态内存池（嵌入式量产）/ Linux原生malloc（调试）
 *  2. 编译期宏 USE_TLSF 控制模式切换，无运行时性能损耗
 *  3. 提供标准分配、清零分配、对齐分配、内存释放接口
 *  4. 内置线程互斥锁，多线程并发调用安全可靠
 *  5. 接口行为与标准C库 malloc/calloc/memalign/free 完全一致
 *  6. 专为 FrameLink/DataBus/视频帧/AI推理 场景设计
 * @author         FrameLink System Team
 * @date           2025
 ******************************************************************************
 */
#ifndef __MEM_ADAPTER_H__
#define __MEM_ADAPTER_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// @brief 内存模式编译宏
// @note  1 = 使用TLSF静态内存池（推荐量产/嵌入式Linux）
// @note  0 = 使用Linux原生malloc/free（推荐开发调试）
// ==========================================================================
#define USE_TLSF    1

// ==========================================================================
// 公共接口声明
// ==========================================================================

/**
 * @brief  初始化内存适配层
 * @param  pool: 静态内存池起始地址（仅USE_TLSF=1时需要传入，原生模式传NULL即可）
 * @param  pool_size: 静态内存池总大小（仅USE_TLSF=1时有效）
 * @return 无
 * @note   系统启动时仅调用一次，必须在所有内存分配接口之前执行
 */
void mem_init(void *pool, size_t pool_size);

/**
 * @brief  销毁内存适配层，释放底层资源
 * @return 无
 * @note   系统退出时调用
 */
void mem_destroy(void);

/**
 * @brief  内存分配（等价于标准malloc，不清零）
 * @param  size: 待分配的内存字节数
 * @retval 成功：返回内存指针；失败：返回NULL
 * @note   分配的内存未初始化（脏数据），性能最优
 * @note   视频帧、数据缓冲推荐使用此接口
 */
void *mem_alloc(size_t size);

/**
 * @brief  清零内存分配（等价于标准calloc，自动清零）
 * @param  num: 元素数量
 * @param  size: 单个元素的字节大小
 * @retval 成功：返回内存指针（全体置0）；失败：返回NULL
 * @note   分配后会执行memset清零，有一定性能开销
 * @note   仅用于状态结构体、计数器等需要初始化为0的场景
 */
void *mem_calloc(size_t num, size_t size);

/**
 * @brief  对齐内存分配（硬件/AI/视频专用）
 * @param  align: 内存对齐字节数（必须为2的幂，如32/64）
 * @param  size: 待分配的内存字节数
 * @retval 成功：返回对齐后的内存指针；失败：返回NULL
 * @note   摄像头帧、NPU/AI推理、DMA操作必须使用对齐分配
 */
void *mem_memalign(size_t align, size_t size);

/**
 * @brief  内存释放（等价于标准free）
 * @param  ptr: 待释放的内存指针
 * @return 无
 * @note   允许传入NULL，此时不执行任何操作
 * @note   必须释放由本模块分配的指针，禁止混合释放
 */
void mem_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* __MEM_ADAPTER_H__ */