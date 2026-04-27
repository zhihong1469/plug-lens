#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "log.h"
#include "v4l2_video.h"

// ==========================================================================
// 全局变量（用于信号处理）
// ==========================================================================
static volatile sig_atomic_t g_running = 1;

// ==========================================================================
// 信号处理函数：Ctrl+C 优雅退出
// ==========================================================================
static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
    LOG_I("Received signal, exiting...");
}

// ==========================================================================
// 主函数
// ==========================================================================
int main(int argc, char **argv)
{
    v4l2_video_config_t config;
    v4l2_video_frame_t frame;
    v4l2_video_err_t err;
    int frame_count = 0;

    // 1. 参数检查
    if (argc < 2) {
        printf("Usage: %s <video_device>\n", argv[0]);
        printf("Example: %s /dev/video0\n", argv[0]);
        return -1;
    }

    // 2. 注册信号处理（Ctrl+C 退出）
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // 3. 配置采集参数（AI视觉专用 YUYV 推荐配置）
    memset(&config, 0, sizeof(config));
    config.dev_path = argv[1];       // 设备路径（从命令行传入）
    config.width = 640;               // 宽度：640（AI常用，平衡性能与精度）
    config.height = 480;              // 高度：480
    config.format = V4L2_PIX_FMT_YUYV;// 格式：YUYV 4:2:2（无压缩，AI首选）
    config.fps = 15;                  // 帧率：15fps（足够AI推理）
    config.buf_count = 4;             // 缓冲区：4个（平衡内存与流畅度）
    config.lock_exposure = true;      // 锁定曝光（AI推理要求画面稳定）
    config.lock_white_balance = true; // 锁定白平衡
    config.lock_gain = true;          // 锁定增益

    // 4. 初始化 V4L2
    LOG_I("Starting V4L2 capture demo...");
    err = v4l2_video_init(&config);
    if (err != V4L2_VIDEO_OK) {
        LOG_E("Init failed: %s", v4l2_video_err_str(err));
        return -1;
    }
    const v4l2_video_capability_t *cap = v4l2_video_get_capability();
    LOG_I("========================================");
    LOG_I("Final Camera Capability Report:");
    LOG_I("  YUYV Support: %s", cap->support_yuyv ? "Yes" : "No");
    LOG_I("  MJPEG Support: %s", cap->support_mjpeg ? "Yes" : "No");
    LOG_I("  Manual Exposure: %s", cap->support_manual_exposure ? "Yes" : "No");
    LOG_I("  Lock WB: %s", cap->support_lock_white_balance ? "Yes" : "No");
    LOG_I("  Lock Gain: %s", cap->support_lock_gain ? "Yes" : "No");
    LOG_I("========================================");

    // 5. 启动采集流
    err = v4l2_video_start();
    if (err != V4L2_VIDEO_OK) {
        LOG_E("Start failed: %s", v4l2_video_err_str(err));
        v4l2_video_deinit();
        return -1;
    }
    LOG_I("Capture started, press Ctrl+C to exit");

    // 6. 主循环：采集帧 → 处理 → 放回
    while (g_running) {
        // 6.1 获取一帧（会阻塞直到数据到来，或超时3秒）
        err = v4l2_video_get_frame(&frame);
        if (err != V4L2_VIDEO_OK) {
            LOG_E("Get frame failed: %s", v4l2_video_err_str(err));
            // 注意：这里不用 break，继续尝试下一帧（可能是临时错误）
            usleep(100000); // 100ms 后重试
            continue;
        }

        // 6.2 【核心】这里处理数据（AI推理、图像保存等）
        // 目前只是打印信息，后续你可以在这里对接 AI 模型
        frame_count++;
        LOG_I("Frame #%d: %ux%u, format=%d, len=%u bytes, ts=%llu us",
              frame_count,
              frame.width, frame.height,
              frame.format,
              frame.length,
              frame.timestamp);

        // 【可选】模拟 AI 推理耗时（比如 50ms）
        // usleep(50000);

        // 6.3 把帧放回驱动队列（非常重要！必须和 get_frame 配对）
        err = v4l2_video_put_frame(&frame);
        if (err != V4L2_VIDEO_OK) {
            LOG_E("Put frame failed: %s", v4l2_video_err_str(err));
            // 即使失败也继续，不要让程序崩溃
        }
    }

    // 7. 优雅退出：停止流 + 释放资源
    LOG_I("Stopping capture...");
    err = v4l2_video_stop();
    if (err != V4L2_VIDEO_OK) {
        LOG_E("Stop failed: %s", v4l2_video_err_str(err));
    }
    v4l2_video_deinit();
    LOG_I("Demo exited successfully, total frames: %d", frame_count);

    return 0;
}
