// 核心头文件
#include "camera_usb.h"
#include "frame_link.h"
#include "data_bus.h"
#include "event_bus.h"
#include "net_push_srv.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

// ====================== 极简配置 ======================
#define DEV_PATH        "/dev/video1"
#define WIDTH           640
#define HEIGHT          360
#define FPS             5
#define FORMAT          V4L2_PIX_FMT_YUYV

#define POOL_CAP        12
#define QUEUE_CAP       6
#define PUSH_PORT       8888

// DataBus配置
#define BUS_MAX_ITEMS       4
#define BUS_ITEM_SIZE       (WIDTH * HEIGHT * 2)
#define BUS_SUBSCRIBERS     2

// ====================== 全局退出标志 ======================
static volatile bool g_running = true;
static camera_base_t        *g_cam = NULL;
static frame_link_handle_t  g_fl = NULL;
static data_bus_handle_t    g_data_bus = NULL;
static event_bus_handle_t   g_event_bus = NULL;
static net_push_srv_handle_t g_net_push = NULL;

// ====================== 信号退出 ======================
static void sig_handler(int sig)
{
    printf("\n[NET_DEMO] 退出...\n");
    g_running = false;
    event_bus_publish_simple(g_event_bus, EVENT_TYPE_SYS_STOP, "MAIN");
}

// ====================== 采集线程：只做一件事 → 发布到DataBus ======================
static void* capture_thread(void *arg)
{
    printf("[NET_DEMO] 采集线程启动 → 数据入DataBus\n");
    while (g_running) {
        frame_t *frame = NULL;
        if (frame_link_get_free_frame(g_fl, &frame) != 0) {
            usleep(1000);
            continue;
        }

        // 读取摄像头数据（唯一真实数据写入）
        void *cam_buf = NULL;
        size_t cam_len = 0;
        if (camera_get_frame(g_cam, &cam_buf, &cam_len) != 0) {
            frame_link_return_free_frame(g_fl, frame);
            usleep(10000);
            continue;
        }

        // 写入FrameLink内存（唯一真实内存拷贝）
        memcpy(frame->data, cam_buf, cam_len);
        frame->width = WIDTH;
        frame->height = HEIGHT;
        frame->format = FRAME_FMT_YUYV;

        // 入队FrameLink（内部管理）
        frame_link_enqueue_frame(g_fl, frame);

        // ============== 核心：发布指针到DataBus ==============
        data_bus_item_handle_t bus_item = NULL;
        if (data_bus_alloc(g_data_bus, DATA_TYPE_VIDEO_FRAME, cam_len, "CAPTURE", &bus_item) == 0) {
            void *w_ptr = data_bus_get_writable_ptr(bus_item);
            memcpy(w_ptr, frame->data, cam_len);
            data_bus_publish(g_data_bus, bus_item);
        }

        usleep(1000000 / FPS);
    }
    return NULL;
}

// ====================== 主函数：极简推流测试 ======================
int main(void)
{
    pthread_t cap_tid;
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("========================================\n");
    printf("        NET_DEMO UDP推流最小测试集\n");
    printf("        推流端口：UDP 8888\n");
    printf("  验证：采集→FrameLink→DataBus→推流 链路\n");
    printf("========================================\n");

    log_init(LOG_LEVEL_INFO);

    // 1. 初始化事件总线
    event_bus_config_t event_cfg = {.max_subscribers = 4};
    event_bus_init(&event_cfg, &g_event_bus);

    // 2. 初始化数据总线
    data_bus_config_t data_cfg = {
        .max_items = BUS_MAX_ITEMS,
        .max_item_size = BUS_ITEM_SIZE,
        .max_subscribers = BUS_SUBSCRIBERS
    };
    data_bus_init(&data_cfg, &g_data_bus);

    // 3. 初始化摄像头
    g_cam = camera_usb_create(DEV_PATH, WIDTH, HEIGHT, FORMAT, FPS);
    camera_init(g_cam);

    // 4. 初始化FrameLink（唯一申请真实内存）
    frame_link_config_t fl_cfg = {
        .max_frame_size = WIDTH * HEIGHT * 2,
        .pool_capacity = POOL_CAP,
        .queue_capacity = QUEUE_CAP
    };
    frame_link_init(&fl_cfg, &g_fl);

    // 5. 初始化UDP推流（纯订阅DataBus，无任何内存申请）
    net_push_srv_config_t push_cfg = {
        .bind_ip = "0.0.0.0",
        .bind_port = PUSH_PORT,
        .queue_capacity = 2,
        .data_bus = g_data_bus,
        .event_bus = g_event_bus
    };
    net_push_srv_create(&push_cfg, &g_net_push);
    net_push_srv_start(g_net_push);

    // 6. 启动采集
    camera_start_capture(g_cam);
    pthread_create(&cap_tid, NULL, capture_thread, NULL);

    printf("[NET_DEMO] 运行中 → 打开UDP客户端连接 8888 端口\n");
    while (g_running) {
        event_bus_dispatch(g_event_bus);
        sleep(1);
    }

    // 回收资源
    pthread_join(cap_tid, NULL);
    net_push_srv_destroy(g_net_push);
    camera_stop_capture(g_cam);
    frame_link_deinit(g_fl);
    camera_usb_destroy(g_cam);
    data_bus_deinit(g_data_bus);
    event_bus_deinit(g_event_bus);

    printf("[NET_DEMO] 退出成功\n");
    return 0;
}