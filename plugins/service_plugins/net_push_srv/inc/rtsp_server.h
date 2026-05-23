/**
 * @file rtsp_server.h
 * @brief 嵌入式 MJPEG RTSP 推流服务器接口头文件
 * @details 基于 live555 库实现，支持外部输入 JPEG 图像，对外提供 RTSP 视频流
 *          支持安全启动/停止，线程安全，完整资源释放
 * @author 嵌入式视觉项目
 * @date 2025
 * @copyright GNU Lesser General Public License
 */

#ifndef __RTSP_SERVER_H__
#define __RTSP_SERVER_H__

#include <stdint.h>

// 兼容 C 语言调用（适配项目纯 C 主程序）
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 RTSP 推流服务器
 * @return 0: 启动成功  非0: 启动失败
 * @note 内部创建独立线程，不阻塞主程序，RTSP端口：8554
 */
int rtsp_server_start(void);

/**
 * @brief 向 RTSP 服务器推送 JPEG 格式图像帧
 * @param jpeg_buf: JPEG 图像数据缓冲区指针
 * @param jpeg_size: JPEG 数据长度（字节）
 * @note 必须传入完整的 JPEG 帧（包含 0xFFD8 开头 + 0xFFD9 结尾）
 */
void rtsp_server_push_jpeg(const uint8_t* jpeg_buf, uint32_t jpeg_size);

/**
 * @brief 安全停止 RTSP 推流服务器
 * @return 0: 停止成功  非0: 停止失败
 * @note 释放所有资源、退出线程、关闭网络端口
 */
int rtsp_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif