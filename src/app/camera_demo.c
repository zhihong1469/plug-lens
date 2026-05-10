#include "camera_base.h"
#include "camera_usb.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(void)
{
    int ret;
    camera_base_t *cam = NULL;
    camera_capability_t cap;  // 设备自检能力结构体

    // ====================== 1. 创建USB摄像头对象 ======================
    // 参数：设备路径 / 分辨率 / 格式(YUYV) / 帧率
    cam = camera_usb_create(
        "/dev/video1",        // 你的摄像头设备节点
        640, 480,             // 分辨率
        V4L2_PIX_FMT_YUYV,    // 像素格式
        30                    // 帧率
    );
    if (!cam) {
        printf("[Demo] 创建摄像头失败\n");
        return -1;
    }
    printf("[Demo] 摄像头对象创建成功\n");

    // ====================== 2. 初始化（自动执行全自检） ======================
    ret = camera_init(cam);
    if (ret != 0) {
        printf("[Demo] 摄像头初始化失败，自检未通过\n");
        camera_usb_destroy(cam);
        return -1;
    }
    printf("[Demo] 摄像头初始化成功 ✅\n");

    // ====================== 3. 获取并打印全自检结果 ======================
    camera_get_capability(cam, &cap);
    printf("\n========== 摄像头自检能力报告 ==========\n");
    printf("设备名称: %s\n", cap.device_name);
    printf("总线信息: %s\n", cap.bus_info);
    printf("支持YUYV: %s\n", cap.support_yuyv ? "YES" : "NO");
    printf("支持MJPEG: %s\n", cap.support_mjpeg ? "YES" : "NO");
    printf("支持NV12: %s\n", cap.support_nv12 ? "YES" : "NO");
    printf("支持手动曝光: %s\n", cap.support_exposure ? "YES" : "NO");
    printf("支持手动白平衡: %s\n", cap.support_white_balance ? "YES" : "NO");
    printf("========================================\n\n");

    // ====================== 4. 启动视频采集 ======================
    ret = camera_start_capture(cam);
    if (ret != 0) {
        printf("[Demo] 启动采集失败\n");
        camera_usb_destroy(cam);
        return -1;
    }
    printf("[Demo] 启动采集成功\n");

    // ====================== 5. 采集3帧图像 ======================
    for (int i = 0; i < 3; i++) {
        void *frame_buf = NULL;
        size_t frame_len = 0;

        ret = camera_get_frame(cam, &frame_buf, &frame_len);
        if (ret == 0) {
            printf("[Demo] 采集第%d帧成功: 地址=%p, 长度=%zu\n",
                   i + 1, frame_buf, frame_len);
        } else {
            printf("[Demo] 采集第%d帧失败\n", i + 1);
        }
        sleep(1);
    }

    // ====================== 6. 热配置：设置曝光（演示） ======================
    int exposure = 100;
    ret = camera_set_param(cam, CAMERA_PARAM_SET_EXPOSURE, &exposure);
    if (ret == 0) {
        printf("[Demo] 热配置曝光成功: %d\n", exposure);
    }

    // ====================== 7. 停止采集 + 销毁资源 ======================
    camera_stop_capture(cam);
    camera_usb_destroy(cam);

    printf("[Demo] 程序正常退出\n");
    return 0;
}