/**
 * @file    capture_srv.c
 * @brief   Video Capture Service Implementation
 * @details Internal implementation features:
 *          - Adopts real-time thread for frame capture (CPU0 bound, priority 80)
 *          - DataBus zero-copy mechanism for video frame transmission
 *          - Software frame downsampling for AI performance optimization
 *          - System event-driven state machine (init/start/pause/stop)
 *          - Static singleton instance, no dynamic runtime memory allocation
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */

#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "utils.h"
#include "vision_ai_config.h"
#include "camera_base.h"
#include "camera_factory.h"
#include "thread.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

// ==========================================================================
// Private Module Macros (Configuration from vision_ai_config.h)
// ==========================================================================
#define MODULE_NAME           "CAPTURE"               /* Module name for log and bus identification */
#define MODULE_TAG            "[CAPTURE]"            /* Log tag for capture service */

/* System bus identifiers (global convention) */
#define CAPTURE_EVENT_BUS_NAME        SYS_EVENT_BUS_NAME        /* System event bus name */
#define CAPTURE_DATA_BUS_NAME         VIDEO_DATA_BUS_NAME      /* Exclusive video data bus name */

/* Capture hardware configuration */
#define CAPTURE_DEV_PATH          CONFIG_CAPTURE_DEV_PATH    /* USB camera device node */
#define CAPTURE_WIDTH             GLOBAL_VIDEO_WIDTH       /* Fixed video width */
#define CAPTURE_HEIGHT            GLOBAL_VIDEO_HEIGHT      /* Fixed video height */
#define CAPTURE_FPS               CONFIG_CAPTURE_FPS         /* Camera hardware FPS */
#define CAPTURE_FORMAT_CFG        CONFIG_CAPTURE_FORMAT      /* Camera format config (0=YUYV,1=NV12,2=MJPEG) */
#define CAPTURE_BUF_CNT           CONFIG_CAPTURE_BUF_COUNT   /* Camera internal buffer count */

/* Maximum frame buffer size: 2 bytes per pixel for YUYV format */
#define MAX_FRAME_SIZE            (CAPTURE_HEIGHT * CAPTURE_WIDTH * 2)

/* Service timing parameters */
#define CAP_FRAME_WAIT_US         20000U    /* Frame wait timeout, unit: microseconds (20ms) */
#define CAP_FPS_INTERVAL_MS       1000U     /* FPS statistics interval, unit: milliseconds */

/* AI frame downsampling configuration */
#define AI_TARGET_FPS             GLOBAL_VIDEO_FPS                /* Target FPS for AI processing */
#define FPS_DOWNSAMPLE_STEP       (CAPTURE_FPS / AI_TARGET_FPS)    /* Frame skip step for downsampling */

/* Real-time thread configuration */
#define CAPTURE_THREAD_STACK_SIZE (1024U * 1024U)  /* Thread stack size: 1MB */
#define CAPTURE_RT_PRIORITY       80U              /* Realtime thread priority */
#define CAPTURE_CPU_ID            0U               /* CPU core binding (i.MX6ULL single core) */

// ==========================================================================
// Private Module Structure (Singleton Instance)
// ==========================================================================
/**
 * @brief   Capture service private context structure
 * @details Manages camera handle, thread, state, configuration and statistics
 * @note    Opaque structure, external modules cannot access members directly
 */
typedef struct {
    camera_base_t          *cam;           /* USB camera device handle */
    thread_t                work_thread;   /* Universal real-time worker thread handle */
    pthread_mutex_t         lock;          /* Thread synchronization mutex */
    uint64_t                frame_count;   /* Total captured frame counter */
    uint64_t                last_fps_ts;   /* Timestamp for last FPS calculation */
    uint32_t                width;         /* Video frame width */
    uint32_t                height;        /* Video frame height */
    uint32_t                fps;           /* Camera hardware FPS */
    uint32_t                v4l2_format;   /* V4L2 pixel format identifier */
    uint32_t                downsample_cnt;/* Frame downsampling counter */
    int                     evt_sub_id;    /* Event bus subscription ID */
    bool                    thread_running;/* Worker thread running flag */
    bool                    is_paused;     /* Service pause state flag */
    bool                    is_started;    /* Service start state flag */
} capture_srv_t;

/**
 * @brief   Global singleton instance of capture service
 * @note    Only one instance allowed per process
 */
static capture_srv_t s_capture;

// ==========================================================================
// Private Helper Functions
// ==========================================================================
/**
 * @brief   Convert config index to V4L2 pixel format
 * @param   cfg  Format configuration index (0=YUYV,1=NV12,2=MJPEG)
 * @return  V4L2 standard pixel format code
 */
static uint32_t _capture_get_v4l2_format(int cfg)
{
    switch (cfg) {
        case 0:  return V4L2_PIX_FMT_YUYV;
        case 1:  return V4L2_PIX_FMT_NV12;
        case 2:  return V4L2_PIX_FMT_MJPEG;
        default: return V4L2_PIX_FMT_YUYV;
    }
}

// ==========================================================================
// Core Resource Cleanup Function
// ==========================================================================
/**
 * @brief   Full resource cleanup for capture service
 * @details Safe stop thread, release camera, unsubscribe events, destroy buses
 * @note    Atomic cleanup, ensures no resource leakage
 */
static void capture_srv_cleanup(void)
{
    capture_srv_t *srv = &s_capture;

    LOG_W(MODULE_TAG " Starting full resource release...");

    /* 1. Stop and join worker thread safely */
    thread_stop(&srv->work_thread);
    srv->thread_running = false;
    srv->is_paused = true;

    if (thread_is_running(&srv->work_thread)) {
        thread_join(&srv->work_thread, NULL);
        LOG_I(MODULE_TAG " Worker thread exited safely");
    }

    /* 2. Unsubscribe from system event bus */
    if (srv->evt_sub_id >= 0) {
        event_bus_unsubscribe(CAPTURE_EVENT_BUS_NAME, srv->evt_sub_id);
        srv->evt_sub_id = -1;
        LOG_I(MODULE_TAG " Event subscription cancelled");
    }

    /* 3. Stop and destroy camera */
    if (srv->cam) {
        camera_stop_capture(srv->cam);
        camera_factory_destroy(srv->cam);
        srv->cam = NULL;
        LOG_I(MODULE_TAG " Camera destroyed");
    }

    /* 4. Deinitialize video DataBus */
    data_bus_deinit(CAPTURE_DATA_BUS_NAME);
    LOG_I(MODULE_TAG " Video DataBus destroyed");

    /* 5. Destroy thread mutex */
    pthread_mutex_destroy(&srv->lock);
    LOG_I(MODULE_TAG " Thread mutex destroyed");

    LOG_I(MODULE_TAG " All resources released, service exited safely");
}

// ==========================================================================
// Worker Thread: Core Capture Logic
// ==========================================================================
/**
 * @brief   Capture service worker thread entry
 * @param   arg  Thread input argument (unused)
 * @return  Thread exit status
 * @details Workflow:
 *          1. Check pause state and wait for frame
 *          2. Read raw frame from USB camera
 *          3. Apply AI frame downsampling
 *          4. Allocate buffer from DataBus (zero-copy)
 *          5. Copy frame data and publish to DataBus
 *          6. Release producer reference and update FPS stats
 * @note    Non-blocking design, real-time priority scheduling
 */
static void *capture_work_thread(void *arg)
{
    (void)arg;
    capture_srv_t *srv = &s_capture;
    data_bus_item_handle_t item = NULL;
    uint64_t current_ts;
    struct timeval tv;
    void *cam_buf = NULL;
    size_t cam_len = 0;
    void *writable_buf = NULL;
    int ret = 0;

    LOG_I(MODULE_TAG " Worker thread started | HW: %ux%u@%uFPS | AI Downsample: %uFPS",
          srv->width, srv->height, srv->fps, AI_TARGET_FPS);

    /* Main thread loop: run until thread stop signal */
    while (thread_is_running(&srv->work_thread)) {
        item = NULL;
        cam_buf = NULL;
        writable_buf = NULL;

        /* Skip capture if service paused */
        if (srv->is_paused) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        /* Step 1: Read raw frame from camera */
        if (camera_get_frame(srv->cam, &cam_buf, &cam_len) != 0) {
            usleep(CAP_FRAME_WAIT_US);
            continue;
        }

        /* Step 2: AI frame downsampling control */
        srv->downsample_cnt++;
        bool send_to_bus = (srv->downsample_cnt >= FPS_DOWNSAMPLE_STEP);
        
        if (!send_to_bus) {
            srv->frame_count++;
            goto fps_stats;
        }

        /* Reset downsampling counter */
        srv->downsample_cnt = 0;

        /* Step 3: Allocate idle buffer from DataBus */
        ret = data_bus_alloc(CAPTURE_DATA_BUS_NAME,
                                 DATA_TYPE_VIDEO,
                                 MAX_FRAME_SIZE,
                                 MODULE_NAME,
                                 &item);
        if (ret != DATA_BUS_OK) {
            if (ret == DATA_BUS_ERR_FULL) {
                LOG_D(MODULE_TAG " DataBus pool full, frame dropped");
            }
            goto fps_stats;
        }

        /* Step 4: Get writable pointer and copy frame data */
        writable_buf = data_bus_get_writable_ptr(item);
        if (!writable_buf) {
            LOG_E(MODULE_TAG " Failed to get writable buffer pointer");
            data_bus_release(item);
            item = NULL;
            goto fps_stats;
        }
        
        size_t copy_len = utils_min(cam_len, MAX_FRAME_SIZE);
        memcpy(writable_buf, cam_buf, copy_len);

        /* Step 5: Publish frame to DataBus (zero-copy) */
        ret = data_bus_push(CAPTURE_DATA_BUS_NAME, item);
        if (ret != DATA_BUS_OK) {
            LOG_E(MODULE_TAG " DataBus push failed, ret=%d", ret);
            data_bus_release(item);
            item = NULL;
            goto fps_stats;
        }

        /* Notify subscribers that frame is ready */
        event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_PROTO_READY, MODULE_NAME);

        /* Step 6: Release producer reference (DataBus manages consumer references) */
        data_bus_release(item);
        item = NULL;

        /* FPS Statistics Calculation */
fps_stats:
        gettimeofday(&tv, NULL);
        current_ts = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        if (current_ts - srv->last_fps_ts >= CAP_FPS_INTERVAL_MS) {
            srv->last_fps_ts = current_ts;
            LOG_D(MODULE_TAG " Total FPS: %llu | AI Valid FPS: %u", 
                  srv->frame_count, AI_TARGET_FPS);
            srv->frame_count = 0;
        }
    }

    LOG_I(MODULE_TAG " Worker thread exited");
    return NULL;
}

// ==========================================================================
// Service Start Function
// ==========================================================================
/**
 * @brief   Start capture service and real-time thread
 * @return  0 on success, negative value on failure
 * @pre     Service initialized successfully, camera ready
 * @post    Worker thread running, frame capture and publishing active
 * @thread_safety No, call only once
 */
static int capture_srv_start(void)
{
    capture_srv_t *srv = &s_capture;
    thread_err_t thread_ret;

    /* Start camera hardware capture */
    if (camera_start_capture(srv->cam) != 0) {
        LOG_E(MODULE_TAG " Failed to start camera capture");
        return -1;
    }

    /* Create real-time thread: auto name, stack, priority, CPU affinity */
    thread_ret = thread_create_rt(&srv->work_thread,
                                  "CAPTURE_Work",
                                  CAPTURE_THREAD_STACK_SIZE,
                                  capture_work_thread,
                                  NULL,
                                  CAPTURE_RT_PRIORITY,
                                  CAPTURE_CPU_ID);

    if (thread_ret != THREAD_OK) {
        LOG_E(MODULE_TAG " Failed to create real-time thread, err=%d", thread_ret);
        camera_stop_capture(srv->cam);
        return -1;
    }

    /* Publish service ready events */
    event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_READY, MODULE_NAME);
    event_bus_publish_simple(CAPTURE_EVENT_BUS_NAME, EVENT_TYPE_CAPTURE_RUNNING, MODULE_NAME);

    LOG_I(MODULE_TAG " Service started successfully [RT Priority=80 | CPU0 Bound]");
    return 0;
}

// ==========================================================================
// Event Bus Callback Handler
// ==========================================================================
/**
 * @brief   System event bus callback for capture service
 * @param   event      Pointer to received event
 * @param   user_data  User context data (service instance)
 * @details Handles system commands: ready, pause, resume, stop, shutdown, error
 * @note    Callback runs in event bus thread, keep logic non-blocking
 */
static void _capture_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    capture_srv_t *srv = &s_capture;

    switch (event->type) {
        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG " System ready event received, service prepared");
            break;

        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG " Pause command received");
            srv->is_paused = true;
            break;

        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started) {
                LOG_I(MODULE_TAG " Start command received, initializing capture");
                capture_srv_start();
                srv->is_started = true;
            } else {
                LOG_I(MODULE_TAG " Resume command received, continuing capture");
                srv->is_paused = false;
            }
            break;

        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
            LOG_I(MODULE_TAG " System stop/shutdown command received, cleaning resources");
            capture_srv_cleanup();
            break;

        case EVENT_TYPE_SYS_ERROR:
            LOG_E(MODULE_TAG " Fatal system error received, force resource cleanup!");
            capture_srv_cleanup();
            break;

        default:
            break;
    }
}

// ==========================================================================
// Service Initialization
// ==========================================================================
/**
 * @brief   Initialize capture service resources
 * @return  0 on success, negative value on failure
 * @details Initialize mutex, DataBus, camera, event subscription
 * @pre     System buses and hardware drivers initialized
 * @post    Service in ready state, waiting for start event
 */
static int capture_srv_init(void)
{
    capture_srv_t *srv = &s_capture;
    memset(srv, 0, sizeof(capture_srv_t));

    /* Initialize thread synchronization */
    pthread_mutex_init(&srv->lock, NULL);
    srv->evt_sub_id = -1;
    srv->cam = NULL;
    srv->is_started = false;
    srv->downsample_cnt = 0;

    /* Load hardware configuration */
    srv->width      = CAPTURE_WIDTH;
    srv->height     = CAPTURE_HEIGHT;
    srv->fps        = CAPTURE_FPS;
    srv->v4l2_format = _capture_get_v4l2_format(CAPTURE_FORMAT_CFG);

    /* Initialize video DataBus */
    data_bus_config_t bus_cfg = {0};
    bus_cfg.max_items = CAPTURE_BUF_CNT;
    bus_cfg.max_item_size = MAX_FRAME_SIZE;
    bus_cfg.max_subscribers = CAPTURE_BUF_CNT;
    bus_cfg.name = CAPTURE_DATA_BUS_NAME;
    
    if (data_bus_init(&bus_cfg) != DATA_BUS_OK) {
        LOG_E(MODULE_TAG " Video DataBus initialization failed");
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    /* Create and initialize camera via factory (auto-selects USB/CSI based on platform) */
    camera_config_t cam_cfg = {
        .dev_path   = CAPTURE_DEV_PATH,
        .width      = srv->width,
        .height     = srv->height,
        .fps        = srv->fps,
        .format     = srv->v4l2_format,
        .buf_count  = CAPTURE_BUF_CNT
    };
    srv->cam = camera_factory_create(&cam_cfg);
    if (!srv->cam || camera_init(srv->cam) != 0) {
        LOG_E(MODULE_TAG " Camera initialization failed");
        data_bus_deinit(CAPTURE_DATA_BUS_NAME);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    /* Subscribe to system event bus */
    event_subscriber_t evt_sub = {0};
    evt_sub.event_type = EVENT_TYPE_INVALID;
    evt_sub.callback = _capture_event_cb;
    evt_sub.user_data = srv;
    
    srv->evt_sub_id = event_bus_subscribe(CAPTURE_EVENT_BUS_NAME, &evt_sub);
    if (srv->evt_sub_id < 0) {
        LOG_E(MODULE_TAG " Event bus subscription failed");
        camera_factory_destroy(srv->cam);
        data_bus_deinit(CAPTURE_DATA_BUS_NAME);
        pthread_mutex_destroy(&srv->lock);
        return -1;
    }

    LOG_I(MODULE_TAG " Service initialized [HW: %ux%u@%uFPS | AI Downsample: %uFPS]",
          srv->width, srv->height, srv->fps, AI_TARGET_FPS);
    return 0;
}

// ==========================================================================
// Auto Initialization (System Init Call)
// ==========================================================================
/**
 * @brief   Auto-init entry for capture service
 * @details Registered to system init call, auto-run during device initialization
 * @return  0 on success, negative value on failure
 */
static int _capture_auto_init(void)
{
    if (capture_srv_init() != 0) {
        return -1;
    }
    LOG_I(MODULE_TAG "_capture_auto_init completed, waiting for system start command");
    return 0;
}

/* Register to system device init level */
#include "initcall.h"
MODULE_INIT_LEVEL(INIT_DEVICE, _capture_auto_init);