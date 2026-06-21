/**
 * @file    net_push_srv.c
 * @brief   Network Push Stream Service Implementation
 * @details Internal implementation features:
 *          - Highest real-time priority thread (90) for RTSP streaming
 *          - Event wakeup low-power design (sleep when no RTSP clients)
 *          - Priority frame pulling: face result → raw camera frame
 *          - Dedicated H.264 DataBus for zero-copy encoded frame transmission
 *          - YUYV to H.264 hardware-friendly encoding
 *          - FPS downsampling (14→5FPS) for i.MX6ULL performance optimization
 *          - Strict DataBus V4.0 reference count compliance
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */

// ==========================================================================
// Header Files
// ==========================================================================
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "vision_ai_config.h"
#include "initcall.h"
#include "img_proc_factory.h"
#include "thread.h"

// Third-party dependencies
#include "rtsp_server.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

// ==========================================================================
// Private Module Macros (Auto-adapt to global config, no hardcode)
// ==========================================================================
#define MODULE_NAME               "NET_PUSH"              /* Module identifier for logs & bus */
#define MODULE_TAG                "[NET_PUSH]"             /* Log tag for network push service */

#define NET_PUSH_TARGET_FPS        GLOBAL_VIDEO_FPS        /* Target push FPS from global config */
#define FRAME_INTERVAL_MS          GLOBAL_FRAME_INTERVAL_MS /* Frame interval in milliseconds */

/* Frame wait timeout: 2x frame interval (dynamic with global FPS) */
#define FRAME_WAIT_TIMEOUT_MS     (FRAME_INTERVAL_MS * 2U)
/* H.264 optimal bitrate for 640x360@15FPS: 500kbps */
#define H264_BITRATE              500U

/* Data Bus Identifiers */
#define FACE_RESULT_RGB_DATA_BUS  FACE_YUV_DATA_BUS_NAME   /* Face detection result bus */
#define VIDEO_DATA_BUS            VIDEO_DATA_BUS_NAME     /* Raw camera video bus */
#define SYS_EVENT_BUS             SYS_EVENT_BUS_NAME      /* System event bus */

/* Dedicated H.264 DataBus Configuration */
#define H264_DATA_BUS_NAME        H264_RTSP_DATA_BUS_NAME  /* Exclusive H.264 encoded bus */
#define H264_MAX_FRAME_SIZE       (1024U * 1024U)          /* Max H.264 frame size (1MB) */
#define H264_BUS_MAX_ITEMS        10U                     /* Bus buffer count (anti-drop) */
#define H264_BUS_MAX_SUBSCRIBER   1U                      /* Only RTSP client allowed */

/* Video Parameters */
#define VIDEO_WIDTH               GLOBAL_VIDEO_WIDTH       /* Camera output width */
#define VIDEO_HEIGHT              GLOBAL_VIDEO_HEIGHT      /* Camera output height */
#define H264_GOP                  GLOBAL_VIDEO_FPS         /* H.264 GOP size */

/* Real-time Thread Configuration (Highest Priority) */
#define NET_PUSH_THREAD_STACK_SIZE (1024U * 1024U)  /* Thread stack size: 1MB */
#define NET_PUSH_RT_PRIORITY       90U              /* Highest real-time priority */
#define NET_PUSH_CPU_ID            0U               /* Bind to CPU0 (i.MX6ULL) */

/* FPS Optimization: 14FPS Capture → 5FPS Push */
#define FPS_DOWNSAMPLE_STEP           2U               /* Process 1 frame every 2 events */
#define TARGET_PUSH_FPS               5U               /* Target RTSP push FPS */

// ==========================================================================
// Private Service Context Structure (Singleton)
// ==========================================================================
/**
 * @brief   Network push stream service control block
 * @details Manages thread, synchronization, encoder, bus handles and RTSP state
 * @note    Opaque singleton structure, external modules cannot access members
 * @warning Direct modification of internal members is forbidden
 */
typedef struct {
    thread_t                work_thread;        /* Universal real-time thread handle */
    pthread_mutex_t         mutex;              /* Mutex for condition variable */
    pthread_cond_t          cond;               /* Event wakeup condition variable */
    bool                    is_paused;          /* Service pause flag */
    bool                    is_started;         /* Service start flag */

    int                     evt_sys_sub_id;     /* System event bus subscription ID */
    int                     evt_capture_sub_id; /* Capture event subscription ID */

    img_proc_handle_t       *img_proc_handle;   /* Image processing factory handle */
    h264_encoder_t          h264_enc;           /* H.264 encoder handle */
    uint32_t                frame_sample_cnt;    /* FPS downsampling counter */

    uint8_t                 sps_pps_cache[256];  /* H.264 SPS/PPS parameter cache */
    uint32_t                sps_pps_len;         /* Length of SPS/PPS data */
    bool                    rtsp_started;        /* RTSP server running flag */
    bool                    last_rtsp_client_state; /* Last RTSP client connection state */
} net_push_srv_t;

/**
 * @brief   Global singleton instance
 * @note    Only one instance allowed in the process
 */
static net_push_srv_t s_net_push_srv;

// ==========================================================================
// Static Function Declarations (Lifecycle Order: Init → Start → Work → Cleanup)
// ============================================================================
static void  net_push_event_cb(const event_t *event, void *user_data);
static void *net_push_work_thread(void *arg);
static int   net_push_srv_start(void);
static void  net_push_srv_cleanup(void);
static int   net_push_srv_init(void);
static int   net_push_srv_auto_init(void);

// ==========================================================================
// H.264 NAL Unit Debug Printer
// ============================================================================
/**
 * @brief   Print complete H.264 NAL units (supports multiple NAL in one frame)
 * @param   h264_data  Pointer to H.264 encoded frame data
 * @param   data_len   Total length of H.264 frame
 * @details Parses and prints SPS/PPS/IDR/P frames for debugging
 * @note    Debug only, disabled in release build
 */
static void net_push_print_h264_nal(const uint8_t* h264_data, int data_len)
{
    if (!h264_data || data_len <= 4) {
        return;
    }

    int pos = 0;
    const int max_pos = data_len - 4;

    while (pos <= max_pos) {
        // Detect H.264 standard start code (0x000001 or 0x00000001)
        int start_code_len = 0;
        if (h264_data[pos] == 0x00 && h264_data[pos+1] == 0x00 && h264_data[pos+2] == 0x01) {
            start_code_len = 3;
        } else if (h264_data[pos] == 0x00 && h264_data[pos+1] == 0x00 && h264_data[pos+2] == 0x00 && h264_data[pos+3] == 0x01) {
            start_code_len = 4;
        } else {
            pos++;
            continue;
        }

        // Extract NAL unit type
        uint8_t nal_type = h264_data[pos + start_code_len] & 0x1F;
        int nal_size = 0;

        // Calculate current NAL unit size
        int next_pos = pos + start_code_len;
        while (next_pos <= max_pos) {
            if ((next_pos + 3 <= max_pos && h264_data[next_pos] == 0x00 && h264_data[next_pos+1] == 0x00 && h264_data[next_pos+2] == 0x01) ||
                (next_pos + 4 <= max_pos && h264_data[next_pos] == 0x00 && h264_data[next_pos+1] == 0x00 && h264_data[next_pos+2] == 0x00 && h264_data[next_pos+3] == 0x01)) {
                break;
            }
            next_pos++;
        }
        nal_size = next_pos - pos;

        // Debug log output
        printf("[NET_PUSH_DEBUG] NAL: Type=0x%02X | Size=%d bytes | Total Frame=%d\n",
               nal_type, nal_size, data_len);
        pos = next_pos;
    }
    printf("----------------------------------------\n");
}

// ==========================================================================
// Helper: Millisecond-precision Conditional Timed Wait
// ============================================================================
/**
 * @brief   Millisecond-precision conditional timed wait
 * @param   cond        Condition variable pointer
 * @param   mutex       Mutex pointer
 * @param   timeout_ms  Timeout value in milliseconds
 * @return  0 on success, error code on timeout/failure
 * @pre     Mutex must be locked before calling
 * @post    Mutex remains locked after return
 * @thread_safety Yes
 */
static int pthread_cond_timedwait_ms(pthread_cond_t *cond,
                                     pthread_mutex_t *mutex,
                                     uint32_t timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000UL;

    if (ts.tv_nsec >= 1000000000UL) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000UL;
    }

    return pthread_cond_timedwait(cond, mutex, &ts);
}

// ==========================================================================
// Event Bus Callback Handler
// ============================================================================
/**
 * @brief   System & capture event callback function
 * @param   event      Pointer to received event object
 * @param   user_data  User-defined context data (unused)
 * @details Handles frame ready, system start/pause/stop events
 * @note    Runs in event bus thread, non-blocking logic only
 * @thread_safety Yes
 */
static void net_push_event_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    net_push_srv_t *srv = &s_net_push_srv;

    switch (event->type)
    {
        /* Wake up push thread when camera frame is ready */
        case EVENT_TYPE_CAPTURE_PROTO_READY:
            if (thread_is_running(&srv->work_thread) && !srv->is_paused)
            {
                pthread_mutex_lock(&srv->mutex);
                pthread_cond_signal(&srv->cond);
                pthread_mutex_unlock(&srv->mutex);
            }
            break;

        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started)
            {
                net_push_srv_start();
                srv->is_started = true;
                LOG_I(MODULE_TAG "System resume, start push service");
            }
            else
            {
                srv->is_paused = false;
                LOG_I(MODULE_TAG "Service resumed");
            }
            break;

        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG "Service entered pause state");
            srv->is_paused = true;
            break;

        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
        case EVENT_TYPE_SYS_ERROR:
            net_push_srv_cleanup();
            break;

        default:
            break;
    }
}

// ==========================================================================
// Core Push Stream Worker Thread
// ============================================================================
/**
 * @brief   Network push stream worker thread entry
 * @param   arg  Thread input argument (unused)
 * @return  Thread exit status
 * @details Workflow:
 *          1. Event wakeup + FPS downsampling
 *          2. RTSP client state detection
 *          3. Pull latest camera frame (priority mode)
 *          4. H.264 encode directly to DataBus (zero-copy)
 *          5. Publish H.264 frame and release resources
 *          6. Low-power sleep when no clients
 * @note    Highest priority, low-power design, i.MX6ULL optimized
 * @thread_safety Yes, DataBus reference count compliant
 */
static void *net_push_work_thread(void *arg)
{
    net_push_srv_t *srv = &s_net_push_srv;
    data_bus_item_handle_t frame_item = NULL;
    data_bus_item_handle_t h264_item = NULL;
    const uint8_t *frame_data = NULL;
    size_t frame_size = 0;
    int h264_len = 0;
    uint8_t *h264_wbuf = NULL;

    srv->last_rtsp_client_state = ( rtsp_has_clients() && srv->rtsp_started );
    struct timespec last_ts;
    clock_gettime(CLOCK_MONOTONIC, &last_ts);
    LOG_I(MODULE_TAG "Push worker thread started, waiting for video data...");

    /* Main thread loop */
    while (thread_is_running(&srv->work_thread))
    {
        /* Low-power wait in pause state */
        if (srv->is_paused) {
            thread_sleep_ms(50);
            continue;
        }

        /* Wait for capture event wakeup */
        pthread_mutex_lock(&srv->mutex);
        pthread_cond_timedwait_ms(&srv->cond, &srv->mutex, FRAME_WAIT_TIMEOUT_MS);
        pthread_mutex_unlock(&srv->mutex);

        /* FPS downsampling control: 14→5FPS */
        srv->frame_sample_cnt++;
        if (srv->frame_sample_cnt < FPS_DOWNSAMPLE_STEP)
        {
            continue;
        }
        srv->frame_sample_cnt = 0;

        /* RTSP client state management */
        bool current_client_state = ( rtsp_has_clients() && srv->rtsp_started );
        if (current_client_state != srv->last_rtsp_client_state)
        {
            srv->last_rtsp_client_state = current_client_state;
            if (current_client_state) {
                event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_RTSP_CONNECTED, MODULE_NAME);
                LOG_I(MODULE_TAG "RTSP client connected, pause face capture");
            } else {
                event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_RTSP_DISCONNECTED, MODULE_NAME);
                LOG_I(MODULE_TAG "RTSP client disconnected, resume face capture");
            }
        }

        /* Core logic: Encode & push only when RTSP client exists */
        if (current_client_state)
        {
            if (data_bus_pull_latest(VIDEO_DATA_BUS, DATA_TYPE_VIDEO, &frame_item) == DATA_BUS_OK)
            {
                frame_data = data_bus_get_readonly_ptr(frame_item);
                frame_size = data_bus_get_item_size(frame_item);

                if (frame_data && frame_size)
                {
                    /* Allocate buffer from dedicated H.264 DataBus */
                    if (data_bus_alloc(H264_DATA_BUS_NAME,
                                       DATA_TYPE_H264,
                                       H264_MAX_FRAME_SIZE,
                                       MODULE_NAME,
                                       &h264_item) == DATA_BUS_OK)
                    {
                        h264_wbuf = data_bus_get_writable_ptr(h264_item);
                        h264_len = H264_MAX_FRAME_SIZE;

                        /* YUYV to H.264 encoding (direct bus write, zero-copy) */
                        img_proc_err_t ret_h = srv->img_proc_handle->ops->yuyv_to_h264(
                            srv->img_proc_handle, srv->h264_enc, frame_data, frame_size, h264_wbuf, &h264_len);
                        if ( ret_h == IMG_PROC_OK)
                        {
                            data_bus_set_item_size(h264_item, h264_len);
                            /* Publish encoded frame to H.264 DataBus */
                            data_bus_push(H264_DATA_BUS_NAME, h264_item);
                        }
                        else if(ret_h == IMG_PROC_ERR_ENCODE)
                        {
                            LOG_I(MODULE_TAG "Performance limit, frame skipped normally");
                        }
                        else
                        {
                            LOG_E(MODULE_TAG "YUYV to H.264 encode failed");
                        }
                        /* Release producer reference (DataBus management) */
                        data_bus_release(h264_item);
                        h264_item = NULL;
                    }
                }
                data_bus_release(frame_item);
                frame_item = NULL;
            }
        }
        else
        {
            /* Low-power sleep when no clients */
            thread_sleep_ms(FRAME_INTERVAL_MS);
        }
    }

    LOG_I(MODULE_TAG "Push worker thread exited safely");
    return NULL;
}

// ==========================================================================
// Service Start Function
// ============================================================================
/**
 * @brief   Start network push service and real-time thread
 * @return  0 on success, negative value on failure
 * @details Initialize H.264 DataBus, encoder, RTSP server and worker thread
 * @pre     Service initialized successfully (net_push_srv_init)
 * @post    Thread running, RTSP ready, H.264 encoding enabled
 * @thread_safety No, call only once
 */
static int net_push_srv_start(void)
{
    net_push_srv_t *srv = &s_net_push_srv;
    thread_err_t thread_ret;
    int ret = -1;

    /* Step 1: Initialize dedicated H.264 DataBus */
    data_bus_config_t h264_bus_cfg = {
        .max_item_size = H264_MAX_FRAME_SIZE,
        .max_items = H264_BUS_MAX_ITEMS,
        .max_subscribers = H264_BUS_MAX_SUBSCRIBER,
        .name = H264_DATA_BUS_NAME,
    };
    if (data_bus_init(&h264_bus_cfg) != DATA_BUS_OK) {
        LOG_E(MODULE_TAG "H264 DataBus initialization failed");
        return -1;
    }
    LOG_I(MODULE_TAG "H264 DataBus initialized successfully");

    /* Initialize condition variable */
    ret = pthread_cond_init(&srv->cond, NULL);
    if (ret != 0) {
        LOG_E(MODULE_TAG "Condition variable initialization failed");
        data_bus_deinit(H264_DATA_BUS_NAME);
        return -1;
    }

    /* Step 2: Create image processing factory handle */
    img_proc_config_t img_proc_cfg = {
        .width = VIDEO_WIDTH,
        .height = VIDEO_HEIGHT,
        .fps = NET_PUSH_TARGET_FPS,
        .bitrate = H264_BITRATE,
        .gop = H264_GOP,
        .jpeg_quality = 50
    };
    srv->img_proc_handle = img_proc_factory_create(&img_proc_cfg);
    if (!srv->img_proc_handle) {
        LOG_E(MODULE_TAG "Image processing factory creation failed");
        pthread_cond_destroy(&srv->cond);
        data_bus_deinit(H264_DATA_BUS_NAME);
        return -1;
    }

    /* Initialize image processing module */
    if (srv->img_proc_handle->ops->init(srv->img_proc_handle) != IMG_PROC_OK) {
        LOG_E(MODULE_TAG "Image processing module initialization failed");
        img_proc_factory_destroy(srv->img_proc_handle);
        pthread_cond_destroy(&srv->cond);
        data_bus_deinit(H264_DATA_BUS_NAME);
        return -1;
    }

    /* Step 3: Create H.264 encoder through factory */
    h264_enc_config_t enc_cfg = {
        .width = VIDEO_WIDTH,
        .height = VIDEO_HEIGHT,
        .fps = NET_PUSH_TARGET_FPS/FPS_DOWNSAMPLE_STEP,
        .bitrate = H264_BITRATE,
        .gop = H264_GOP,
        .use_cpu_core = true
    };
    LOG_I(MODULE_TAG "Create H264 encoder | %dx%d | %dFPS | GOP=%d",
          VIDEO_WIDTH, VIDEO_HEIGHT, NET_PUSH_TARGET_FPS, H264_GOP);
    srv->h264_enc = srv->img_proc_handle->ops->h264_encoder_create(srv->img_proc_handle, &enc_cfg);
    if (!srv->h264_enc) {
        LOG_E(MODULE_TAG "H.264 encoder creation failed");
        srv->img_proc_handle->ops->deinit(srv->img_proc_handle);
        img_proc_factory_destroy(srv->img_proc_handle);
        pthread_cond_destroy(&srv->cond);
        data_bus_deinit(H264_DATA_BUS_NAME);
        return -2;
    }

    /* Step 4: Get and set H.264 SPS/PPS for RTSP */
    srv->sps_pps_len = sizeof(srv->sps_pps_cache);
    if (srv->img_proc_handle->ops->h264_encoder_get_sps_pps(
        srv->img_proc_handle, srv->h264_enc, srv->sps_pps_cache, &srv->sps_pps_len) == IMG_PROC_OK)
    {
        LOG_I(MODULE_TAG "Get SPS+PPS success | Size: %d bytes", srv->sps_pps_len);
        rtsp_set_sps_pps(srv->sps_pps_cache, srv->sps_pps_len);
        if (rtsp_start_service() == 0) {
            srv->rtsp_started = true;
            LOG_I(MODULE_TAG "RTSP service started successfully");
        }
    }

    /* Create highest-priority real-time thread */
    thread_ret = thread_create_rt(&srv->work_thread,
                                  "NET_Push",
                                  NET_PUSH_THREAD_STACK_SIZE,
                                  net_push_work_thread,
                                  NULL,
                                  NET_PUSH_RT_PRIORITY,
                                  NET_PUSH_CPU_ID);

    if (thread_ret != THREAD_OK) {
        LOG_E(MODULE_TAG "Realtime push thread creation failed err=%d", thread_ret);
        srv->img_proc_handle->ops->h264_encoder_destroy(srv->img_proc_handle, srv->h264_enc);
        srv->img_proc_handle->ops->deinit(srv->img_proc_handle);
        img_proc_factory_destroy(srv->img_proc_handle);
        pthread_cond_destroy(&srv->cond);
        data_bus_deinit(H264_DATA_BUS_NAME);
        return -5;
    }

    srv->is_paused = false;
    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_NET_READY, MODULE_NAME);
    LOG_I(MODULE_TAG "Push service started [Priority=90 | CPU0 Bound]");
    return 0;
}

// ==========================================================================
// Service Resource Cleanup
// ============================================================================
/**
 * @brief   Full resource cleanup for network push service
 * @details Safe thread stop, encoder release, bus destruction, RTSP stop
 * @note    Atomic cleanup, no resource leakage
 * @pre     Service is running or paused
 * @post    All resources released, service stopped completely
 * @thread_safety No, called on system stop/error
 */
static void net_push_srv_cleanup(void)
{
    net_push_srv_t *srv = &s_net_push_srv;

    LOG_W(MODULE_TAG "Starting full resource release");
    thread_stop(&srv->work_thread);
    srv->is_paused = true;

    /* Wake up blocked thread to exit */
    pthread_mutex_lock(&srv->mutex);
    pthread_cond_signal(&srv->cond);
    pthread_mutex_unlock(&srv->mutex);

    if (thread_is_running(&srv->work_thread)) {
        thread_join(&srv->work_thread, NULL);
    }

    /* Unsubscribe from event buses */
    if (srv->evt_sys_sub_id >= 0) event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_sys_sub_id);
    if (srv->evt_capture_sub_id >= 0) event_bus_unsubscribe(SYS_EVENT_BUS, srv->evt_capture_sub_id);

    /* Release H.264 encoder through factory */
    if (srv->h264_enc && srv->img_proc_handle) {
        srv->img_proc_handle->ops->h264_encoder_destroy(srv->img_proc_handle, srv->h264_enc);
        srv->h264_enc = NULL;
    }

    /* Release image processing factory handle */
    if (srv->img_proc_handle) {
        srv->img_proc_handle->ops->deinit(srv->img_proc_handle);
        img_proc_factory_destroy(srv->img_proc_handle);
        srv->img_proc_handle = NULL;
    }

    /* Stop RTSP server and destroy H.264 DataBus */
    rtsp_server_stop();
    data_bus_deinit(H264_DATA_BUS_NAME);
    srv->rtsp_started = false;
    srv->sps_pps_len = 0;

    /* Destroy synchronization primitives */
    pthread_cond_destroy(&srv->cond);
    pthread_mutex_destroy(&srv->mutex);

    event_bus_publish_simple(SYS_EVENT_BUS, EVENT_TYPE_NET_STOPPED, MODULE_NAME);
    LOG_I(MODULE_TAG "All resources released successfully");
}

// ==========================================================================
// Service Initialization
// ============================================================================
/**
 * @brief   Initialize network push service resources
 * @return  0 on success, negative value on failure
 * @details Initialize mutex, event subscription, context structure
 * @pre     System buses and drivers initialized
 * @post    Service ready to start on system resume
 * @thread_safety No, called once during auto-init
 */
static int net_push_srv_init(void)
{
    net_push_srv_t *srv = &s_net_push_srv;
    int ret = -1;

    /* Clear context structure */
    memset(srv, 0, sizeof(net_push_srv_t));
    srv->evt_sys_sub_id = -1;
    srv->evt_capture_sub_id = -1;
    srv->frame_sample_cnt = 0;

    /* Initialize thread mutex */
    ret = pthread_mutex_init(&srv->mutex, NULL);
    if (ret != 0) {
        LOG_E(MODULE_TAG "Mutex initialization failed");
        return -1;
    }

    /* Subscribe to system event bus */
    event_subscriber_t sys_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = net_push_event_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_sys_sub_id = event_bus_subscribe(SYS_EVENT_BUS, &sys_sub);

    if (srv->evt_sys_sub_id < 0 )
    {
        LOG_E(MODULE_TAG "Event subscription failed");
        net_push_srv_cleanup();
        return -3;
    }

    LOG_I(MODULE_TAG "Network push service initialized");
    return 0;
}

// ==========================================================================
// Auto Initialization (System Init Call)
// ============================================================================
/**
 * @brief   Auto-init entry for system service level
 * @return  0 on success, negative value on failure
 * @note    Registered via MODULE_INIT_LEVEL, auto-run on system boot
 */
static int net_push_srv_auto_init(void)
{
    if (net_push_srv_init() != 0) return -1;
    LOG_I(MODULE_TAG "Module auto-load completed, waiting for system start command");
    return 0;
}

/* Register to system service initialization level */
MODULE_INIT_LEVEL(INIT_SERVICE, net_push_srv_auto_init);

/******************************* End of file **********************************/