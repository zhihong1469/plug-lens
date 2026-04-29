#include <signal.h>
#include <string.h>
#include "v4l2_video.h"
#include "log.h"


static volatile sig_atomic_t g_running = 1;

void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

int main(int argc, char **argv) {
    v4l2_app_config_t app_cfg;
    v4l2_video_frame_t *frame = NULL;
    int save_counter = 0;

    // 1. 信号处理
    signal(SIGINT, sig_handler);

    // 2. 填充配置（这里可以从 configs 文件读，或者写死）
    memset(&app_cfg, 0, sizeof(app_cfg));
    app_cfg.v4l2_cfg.dev_path = "/dev/video1";
    app_cfg.v4l2_cfg.width = 640;
    app_cfg.v4l2_cfg.height = 480;
    app_cfg.v4l2_cfg.format = V4L2_PIX_FMT_YUYV;
    app_cfg.v4l2_cfg.fps = 30;
    app_cfg.v4l2_cfg.buf_count = 4;
    app_cfg.v4l2_cfg.lock_exposure = true;
    app_cfg.v4l2_cfg.lock_white_balance = true;
    app_cfg.v4l2_cfg.lock_gain = true;
    app_cfg.queue_size = 4; 

    // 3. 初始化 APP 层
    if (v4l2_app_init(&app_cfg) != 0) {
        LOG_E("App init failed");
        return -1;
    }

    // 4. 启动
    v4l2_app_start();
    LOG_I("System running...");

    // 5. 主循环（Main 线程只干一件事：取帧 -> AI 推理）
    while (g_running) {
        // 5.1 从队列取帧（等待 100ms）
        if (v4l2_app_get_frame(&frame, 100) != 0) {
            continue; // 没数据，继续等
        }

        // 5.2 【核心业务】在这里做 AI 推理
        LOG_I("Got frame for AI: %ux%u", frame->width, frame->height);

        // 5.3 【调试】每隔 100 帧存一张图
        if (save_counter++ % 100 == 0) {
            v4l2_app_save_yuv(frame, "/tmp");
        }

        // 5.4 推理完，归还帧（非常重要）
        v4l2_app_release_frame(frame);
    }

    // 6. 优雅退出
    v4l2_app_stop();
    v4l2_app_deinit();
    LOG_I("Exit success");
    return 0;
}
