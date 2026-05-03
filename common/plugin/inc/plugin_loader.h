// common/plugin/plugin_loader.h
#ifndef PLUGIN_LOADER_H
#define PLUGIN_LOADER_H

#include <stdint.h>
#include <stdbool.h>

// ==========================================================================
// 【插件化设计】静态插件接口（嵌入式场景优先静态，避免动态库开销）
// ==========================================================================

// 插件初始化函数类型
typedef int (*plugin_init_func_t)(void);
// 插件启动函数类型
typedef int (*plugin_start_func_t)(void);
// 插件停止函数类型
typedef int (*plugin_stop_func_t)(void);
// 插件销毁函数类型
typedef int (*plugin_deinit_func_t)(void);

// 插件描述符
typedef struct {
    const char *name;           // 插件名称
    plugin_init_func_t init;    // 初始化
    plugin_start_func_t start;  // 启动
    plugin_stop_func_t stop;    // 停止
    plugin_deinit_func_t deinit;// 销毁
} plugin_desc_t;

// ==========================================================================
// 插件加载器接口
// ==========================================================================

/**
 * @brief 注册插件（静态插件在编译时注册）
 * @param desc 插件描述符
 * @return 0成功
 */
int plugin_register(const plugin_desc_t *desc);

/**
 * @brief 初始化所有已注册插件
 * @return 0成功
 */
int plugin_init_all(void);

/**
 * @brief 启动所有已注册插件
 * @return 0成功
 */
int plugin_start_all(void);

/**
 * @brief 停止所有已注册插件
 * @return 0成功
 */
int plugin_stop_all(void);

/**
 * @brief 销毁所有已注册插件
 * @return 0成功
 */
int plugin_deinit_all(void);

#endif /* PLUGIN_LOADER_H */