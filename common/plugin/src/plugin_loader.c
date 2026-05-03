// common/plugin/plugin_loader.c
#include "plugin_loader.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ==========================================================================
// 内部宏定义
// ==========================================================================
#define MAX_PLUGINS 32

// ==========================================================================
// 内部上下文结构体
// ==========================================================================
typedef struct {
    const plugin_desc_t *plugins[MAX_PLUGINS];
    uint32_t plugin_count;
    bool initialized;
    bool started;
    pthread_mutex_t lock;
} plugin_loader_context_t;

// ==========================================================================
// 内部全局变量（单例）
// ==========================================================================
static plugin_loader_context_t g_loader_ctx = {
    .plugin_count = 0,
    .initialized = false,
    .started = false,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

// ==========================================================================
// 对外API实现
// ==========================================================================

int plugin_register(const plugin_desc_t *desc)
{
    if (desc == NULL || desc->name == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_loader_ctx.lock);

    // 检查是否已注册
    for (uint32_t i = 0; i < g_loader_ctx.plugin_count; i++) {
        if (strcmp(g_loader_ctx.plugins[i]->name, desc->name) == 0) {
            pthread_mutex_unlock(&g_loader_ctx.lock);
            return -1; // 已注册
        }
    }

    // 检查插件数量
    if (g_loader_ctx.plugin_count >= MAX_PLUGINS) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return -1;
    }

    // 注册插件
    g_loader_ctx.plugins[g_loader_ctx.plugin_count++] = desc;

    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}

int plugin_init_all(void)
{
    pthread_mutex_lock(&g_loader_ctx.lock);

    if (g_loader_ctx.initialized) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return 0;
    }

    // 初始化所有插件
    for (uint32_t i = 0; i < g_loader_ctx.plugin_count; i++) {
        const plugin_desc_t *desc = g_loader_ctx.plugins[i];
        if (desc->init != NULL) {
            int ret = desc->init();
            if (ret != 0) {
                // 初始化失败，回滚已初始化的插件
                for (uint32_t j = 0; j < i; j++) {
                    const plugin_desc_t *rollback_desc = g_loader_ctx.plugins[j];
                    if (rollback_desc->deinit != NULL) {
                        rollback_desc->deinit();
                    }
                }
                pthread_mutex_unlock(&g_loader_ctx.lock);
                return ret;
            }
        }
    }

    g_loader_ctx.initialized = true;
    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}

int plugin_start_all(void)
{
    pthread_mutex_lock(&g_loader_ctx.lock);

    if (!g_loader_ctx.initialized) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return -1;
    }

    if (g_loader_ctx.started) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return 0;
    }

    // 启动所有插件
    for (uint32_t i = 0; i < g_loader_ctx.plugin_count; i++) {
        const plugin_desc_t *desc = g_loader_ctx.plugins[i];
        if (desc->start != NULL) {
            int ret = desc->start();
            if (ret != 0) {
                // 启动失败，回滚已启动的插件
                for (uint32_t j = 0; j < i; j++) {
                    const plugin_desc_t *rollback_desc = g_loader_ctx.plugins[j];
                    if (rollback_desc->stop != NULL) {
                        rollback_desc->stop();
                    }
                }
                pthread_mutex_unlock(&g_loader_ctx.lock);
                return ret;
            }
        }
    }

    g_loader_ctx.started = true;
    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}

int plugin_stop_all(void)
{
    pthread_mutex_lock(&g_loader_ctx.lock);

    if (!g_loader_ctx.started) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return 0;
    }

    // 停止所有插件（逆序）
    for (int32_t i = g_loader_ctx.plugin_count - 1; i >= 0; i--) {
        const plugin_desc_t *desc = g_loader_ctx.plugins[i];
        if (desc->stop != NULL) {
            desc->stop();
        }
    }

    g_loader_ctx.started = false;
    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}

int plugin_deinit_all(void)
{
    pthread_mutex_lock(&g_loader_ctx.lock);

    if (!g_loader_ctx.initialized) {
        pthread_mutex_unlock(&g_loader_ctx.lock);
        return 0;
    }

    // 先停止
    if (g_loader_ctx.started) {
        plugin_stop_all();
    }

    // 销毁所有插件（逆序）
    for (int32_t i = g_loader_ctx.plugin_count - 1; i >= 0; i--) {
        const plugin_desc_t *desc = g_loader_ctx.plugins[i];
        if (desc->deinit != NULL) {
            desc->deinit();
        }
    }

    g_loader_ctx.initialized = false;
    g_loader_ctx.plugin_count = 0;
    pthread_mutex_unlock(&g_loader_ctx.lock);
    return 0;
}
