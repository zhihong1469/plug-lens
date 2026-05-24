/* SPDX-License-Identifier: MIT */
/**
 ******************************************************************************
 * @file           rtsp_server.h
 * @brief          RTSP Server 对外接口头文件
 * @author         Luo
 * @date           2026
 ******************************************************************************
 */
#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  设置H264的SPS/PPS参数（RTSP启动前必须调用）
 * @param  sps_pps: SPS+PPS数据指针
 * @param  len: 数据长度
 */
void rtsp_set_sps_pps(const uint8_t* sps_pps, uint32_t len);

/**
 * @brief  单独启动RTSP服务
 * @return 0:成功 负数:失败
 */
int rtsp_start_service(void);

/**
 * @brief  判断RTSP服务是否运行
 * @return true:运行中 false:未运行
 */
bool rtsp_is_running(void);

/**
 * @brief  推送H264/JPEG数据到RTSP
 * @param  buf: 帧数据
 * @param  size: 数据长度
 */
void rtsp_server_push(const uint8_t* buf, uint32_t size);

/**
 * @brief  停止RTSP服务
 * @return 0:成功
 */
int rtsp_server_stop(void);

/* 兼容旧接口，无实际作用 */
int rtsp_server_start(void);

#ifdef __cplusplus
}
#endif

#endif // RTSP_SERVER_H