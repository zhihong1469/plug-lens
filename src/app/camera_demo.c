#include <stdio.h>
#include <unistd.h>
// 包含核心基类头文件
#include "camera_base.h"
// 包含USB插件头文件
#include "camera_usb.h"

int main(void)
{
    int ret;
    // 1. 创建USB摄像头对象（设备节点 /dev/video1，分辨率640x480）
    camera_base_t *cam = camera_usb_create("/dev/video1", 640, 480);
    if (!cam)
    {
        printf("摄像头对象创建失败！\n");
        return -1;
    }

    // 2. 初始化设备
    ret = camera_init(cam);
    if (ret < 0)
    {
        printf("摄像头初始化失败！\n");
        goto destroy;
    }

    // 3. 设置参数（帧率30）
    int fps = 30;
    camera_set_param(cam, CAMERA_PARAM_SET_FPS, &fps);

    // 4. 启动视频采集
    ret = camera_start_capture(cam);
    if (ret < 0)
    {
        printf("启动采集失败！\n");
        goto destroy;
    }

    // 5. 循环采集3帧数据（演示）
    for (int i = 0; i < 3; i++)
    {
        void *frame = NULL;
        size_t len = 0;
        ret = camera_get_frame(cam, &frame, &len);
        if (ret == 0)
        {
            printf("采集第%d帧成功：地址=%p，长度=%zu\n", i+1, frame, len);
        }
        sleep(1);
    }

    // 6. 停止采集
    camera_stop_capture(cam);
    printf("采集停止\n");


destroy:
    // 8. 销毁对象，释放所有资源
    camera_usb_destroy(cam);
    printf("资源释放完成，程序退出\n");

    return 0;
}