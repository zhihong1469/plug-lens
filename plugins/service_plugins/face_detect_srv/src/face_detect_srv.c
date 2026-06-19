/**
 * @file    face_detect_srv.c
 * @brief   Face Detection Service Implementation
 * @details Internal implementation features:
 *          - Event wakeup mechanism for low-power AI processing
 *          - 15FPS capture → 5FPS AI detection via frame downsampling
 *          - Dual isolated DataBus: Raw AI RGB + Face Result RGB
 *          - RTSP stream synchronization (pause on connect, resume on disconnect)
 *          - Real-time thread (CPU0 bound, priority 70)
 *          - OpenCV face box drawing + SD card image storage
 *          - Strict DataBus V4.0 reference count compliance
 *
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */

// ==========================================================================
// Private Module Macros (Configuration from vision_ai_config.h)
// ==========================================================================
#define MODULE_NAME               "FACE_DETECT"          /* Module identifier for logs and bus */
#define MODULE_TAG                "[FACE_DETECT]"         /* Log tag for face detection service */

/* Data Bus Identifiers (Global Convention) */
#define VIDEO_DATA_BUS            VIDEO_DATA_BUS_NAME                /* Main camera video bus */
#define AI_RAW_RGB_DATA_BUS       AI_RGB_DATA_BUS_NAME               /* Isolated bus: AI decoded raw RGB */
#define FACE_RESULT_RGB_DATA_BUS  FACE_YUV_DATA_BUS_NAME             /* Isolated bus: RGB with face boxes */
#define CAPTURE_EVENT_BUS         SYS_EVENT_BUS_NAME                 /* System event bus */

/* Dual RGB Bus Resource Configuration */
#define AI_RAW_RGB_POOL_SIZE          4U                    /* Raw RGB buffer pool count */
#define AI_RAW_RGB_MAX_SUBSCRIBERS    4U                    /* Max subscribers for raw RGB bus */
#define FACE_RESULT_RGB_POOL_SIZE     4U                    /* Result RGB buffer pool count */
#define FACE_RESULT_RGB_MAX_SUBSCRIBERS 4U                  /* Max subscribers for result bus */
#define AI_MAX_FACES                  MAX_FACES             /* Max detectable faces per frame */

/* AI Model Parameters */
#define AI_MODEL_PATH                 CONFIG_AI_MODEL_PATH          /* MNN model file path */
#define AI_INPUT_WIDTH                CONFIG_AI_INPUT_W             /* AI model input width */
#define AI_INPUT_HEIGHT               CONFIG_AI_INPUT_H             /* AI model input height */
#define AI_SCORE_THRESH               CONFIG_AI_SCORE_THRESH        /* AI confidence threshold */
#define AI_IOU_THRESH                 CONFIG_AI_IOU_THRESH          /* AI NMS IOU threshold */

/* Camera Video Parameters */
#define CAPTURE_WIDTH                 GLOBAL_VIDEO_WIDTH            /* Camera output width */
#define CAPTURE_HEIGHT                GLOBAL_VIDEO_HEIGHT           /* Camera output height */

/* Real-time Thread Configuration */
#define FACE_THREAD_STACK_SIZE        (1024U * 1024U)  /* Thread stack size: 1MB */
#define FACE_RT_PRIORITY              70U              /* Realtime priority (lower than capture) */
#define FACE_CPU_ID                   0U               /* Bind to CPU0 (i.MX6ULL) */
#define FRAME_WAIT_TIMEOUT_MS         200U             /* Conditional wait timeout */

/* FPS Control Configuration: 15FPS Capture → 5FPS AI Detection */
#define FPS_DOWNSAMPLE_STEP           14U               /* Process 1 frame every 14 events */
#define TARGET_AI_FPS                 5U                /* Target AI processing FPS */

/* Frame Buffer Size (24-bit RGB) */
#define AI_RAW_RGB_FRAME_SIZE         (CAPTURE_WIDTH * CAPTURE_HEIGHT * 3U)
#define FACE_RESULT_RGB_FRAME_SIZE    (CAPTURE_WIDTH * CAPTURE_HEIGHT * 3U)

// ==========================================================================
// Header Files
// ==========================================================================
#include "log.h"
#include "data_bus.h"
#include "event_bus.h"
#include "vision_ai_config.h"
#include "ai_model_base.h"
#include "ai_model_factory.h"
#include "initcall.h"
#include "img_storage.h"
#include "thread.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include "led_base.h"

// ==========================================================================
// Private Service Context Structure (Singleton)
// ==========================================================================
/**
 * @brief   Face detection service control block
 * @details Manages AI model, thread, synchronization, bus handles and state
 * @note    Opaque singleton structure, external modules cannot access members
 * @warning Direct modification of internal members is forbidden
 */
typedef struct {
    ai_model_handle_t            *ai_model;              /* MNN AI model handle */
    thread_t                     work_thread;             /* Universal real-time thread handle */
    pthread_mutex_t               mutex;                  /* Mutex for condition variable */
    pthread_cond_t                cond;                   /* Event wakeup condition variable */
    bool                          is_paused;              /* Service pause flag */
    bool                          is_started;             /* Service start flag */
    int                           evt_sys_sub_id;         /* System event bus subscription ID */
    int                           evt_capture_sub_id;     /* Capture event subscription ID */
    ai_model_detect_result_t      faces[AI_MAX_FACES];    /* Face detection result array */
    int                           face_num;               /* Number of detected faces */
    ImgStorage_t                  *img_storage;           /* SD card image storage handle */
    uint32_t                      frame_sample_cnt;       /* FPS downsampling counter */
    led_base_t                    *s_led;                  /* LED indicator handle */
} face_detect_srv_t;

/**
 * @brief   Global singleton instance
 * @note    Only one instance allowed in the process
 */
static face_detect_srv_t s_face_srv;

// ==========================================================================
// Static Function Declarations (Lifecycle Order)
// ==========================================================================
static void  event_bus_cb(const event_t *event, void *user_data);
static void *face_work_thread(void *arg);
static int   face_srv_start(void);
static void  face_srv_cleanup(void);
static int   face_srv_init(void);
static int   face_srv_auto_init(void);

// ==========================================================================
// Event Bus Callback Handler
// ==========================================================================
/**
 * @brief   System & capture event callback function
 * @param   event      Pointer to received event object
 * @param   user_data  User-defined context data (unused)
 * @details Handles capture ready, RTSP connect/disconnect, system control events
 * @note    Runs in event bus thread, keep logic non-blocking
 * @thread_safety Yes, uses mutex for condition variable signaling
 */
static void event_bus_cb(const event_t *event, void *user_data)
{
    (void)user_data;
    face_detect_srv_t *srv = &s_face_srv;

    switch (event->type)
    {
        /* Wake up AI thread when camera frame is ready (low-power trigger) */
        case EVENT_TYPE_CAPTURE_PROTO_READY:
            if (thread_is_running(&srv->work_thread) && !srv->is_paused)
            {
                pthread_mutex_lock(&srv->mutex);
                pthread_cond_signal(&srv->cond);
                pthread_mutex_unlock(&srv->mutex);
            }
            break;

        /* Pause service when RTSP client connected */
        case EVENT_TYPE_RTSP_CONNECTED:
            if (!srv->is_paused) {
                srv->is_paused = true;
                LOG_I(MODULE_TAG "RTSP streaming active, pause face detection");
            }
            break;

        /* Resume service when RTSP client disconnected */
        case EVENT_TYPE_RTSP_DISCONNECTED:
            if (srv->is_paused) {
                srv->is_paused = false;
                LOG_I(MODULE_TAG "RTSP disconnected, resume face detection");
            }
            break;

        case EVENT_TYPE_SYS_CORE_READY:
            LOG_I(MODULE_TAG "System core initialization completed");
            break;

        case EVENT_TYPE_SYS_PAUSE:
            LOG_I(MODULE_TAG "Service entered pause state");
            srv->is_paused = true;
            break;

        case EVENT_TYPE_SYS_RESUME:
            if (!srv->is_started)
            {
                face_srv_start();
                srv->is_started = true;
            }
            else
            {
                srv->is_paused = false;
                LOG_I(MODULE_TAG "Service resumed");
            }
            break;

        case EVENT_TYPE_SYS_STOP:
        case EVENT_TYPE_SYS_SHUTDOWN:
        case EVENT_TYPE_SYS_ERROR:
            face_srv_cleanup();
            break;

        default:
            break;
    }
}

// ==========================================================================
// Helper Function: Millisecond Conditional Wait
// ==========================================================================
/**
 * @brief   Millisecond-precision conditional timed wait
 * @param   cond        Condition variable pointer
 * @param   mutex       Mutex pointer
 * @param   timeout_ms  Timeout value in milliseconds
 * @return  0 on success, error code on timeout/failure
 * @pre     Mutex must be locked before calling
 * @post    Mutex remains locked after return
 */
int pthread_cond_timedwait_ms(pthread_cond_t *cond,
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
// Core AI Worker Thread
// ==========================================================================
/**
 * @brief   Face detection worker thread entry
 * @param   arg  Thread input argument (unused)
 * @return  Thread exit status
 * @details Workflow:
 *          1. Wait for capture event wakeup (low-power)
 *          2. Frame downsampling (15→5FPS)
 *          3. Pull latest camera frame from DataBus
 *          4. Allocate raw RGB buffer and run AI inference
 *          5. Draw face boxes and publish result to isolated bus
 *          6. Save image to SD card and release resources
 * @note    Non-blocking, event-driven, real-time scheduling
 * @thread_safety Yes, uses DataBus reference count and mutex
 */
static void *face_work_thread(void *arg)
{
    (void)arg;
    face_detect_srv_t *srv = &s_face_srv;
    data_bus_item_handle_t camera_item = NULL;
    data_bus_item_handle_t raw_rgb_item = NULL;
    data_bus_item_handle_t result_rgb_item = NULL;
    int ret;

    LOG_I(MODULE_TAG "AI worker thread started [Pull Mode + Event Wakeup + 5FPS]");

    /* Main thread loop */
    while (thread_is_running(&srv->work_thread))
    {
        /* Low-power wait in pause state */
        if (srv->is_paused)
        {
            thread_sleep_ms(FRAME_WAIT_TIMEOUT_MS);
            continue;
        }

        /* Wait for capture event wakeup */
        pthread_mutex_lock(&srv->mutex);
        pthread_cond_timedwait_ms(&srv->cond, &srv->mutex, FRAME_WAIT_TIMEOUT_MS);
        pthread_mutex_unlock(&srv->mutex);

        /* FPS control: process 1 frame per 14 events (5FPS target) */
        srv->frame_sample_cnt++;
        if (srv->frame_sample_cnt < FPS_DOWNSAMPLE_STEP)
        {
            continue;
        }
        srv->frame_sample_cnt = 0;

        /* Pull the latest camera frame from DataBus */
        ret = data_bus_pull_latest(VIDEO_DATA_BUS, DATA_TYPE_VIDEO, &camera_item);
        if (ret != DATA_BUS_OK || !camera_item)
        {
            continue;
        }
        const uint8_t *src_camera = data_bus_get_readonly_ptr(camera_item);

        /* Allocate buffer for AI raw RGB frame (isolated bus) */
        ret = data_bus_alloc(AI_RAW_RGB_DATA_BUS,
                             DATA_TYPE_VIDEO_RGB,
                             AI_RAW_RGB_FRAME_SIZE,
                             MODULE_NAME,
                             &raw_rgb_item);
        if (ret != DATA_BUS_OK)
        {
            LOG_W(MODULE_TAG "No free buffer in AI raw RGB bus");
            data_bus_release(camera_item);
            camera_item = NULL;
            continue;
        }
        uint8_t *raw_rgb_data = data_bus_get_writable_ptr(raw_rgb_item);

        /* AI Inference: YUYV decode + RGB conversion + face detection */
        srv->face_num = 0;
        memset(srv->faces, 0, sizeof(srv->faces));
        ret = srv->ai_model->ops->infer_image(srv->ai_model,
                                              (uint8_t *)src_camera,
                                              CAPTURE_WIDTH,
                                              CAPTURE_HEIGHT,
                                              raw_rgb_data,
                                              (ai_model_detect_result_t *)srv->faces,
                                              AI_MAX_FACES,
                                              &srv->face_num,
                                              INPUT_FORMAT_YUYV);

        if (ret != AI_MODEL_OK)
        {
            LOG_E(MODULE_TAG "AI inference failed, error code:%d", ret);
            goto release_res;
        }

        /* Skip processing if no face detected */
        if (srv->face_num <= 0)
        {
            LOG_D(MODULE_TAG "No face detected");
            goto release_res;
        }

        LOG_D(MODULE_TAG "Detected %d faces", srv->face_num);
        led_base_turn_on(srv->s_led);

        /* Allocate buffer and draw face boxes (isolated result bus) */
        ret = data_bus_alloc(FACE_RESULT_RGB_DATA_BUS,
                             DATA_TYPE_VIDEO_RGB,
                             FACE_RESULT_RGB_FRAME_SIZE,
                             MODULE_NAME,
                             &result_rgb_item);
        if (ret == DATA_BUS_OK)
        {
            uint8_t *result_rgb_data = data_bus_get_writable_ptr(result_rgb_item);
            if (srv->ai_model->ops->map_and_draw_faces(srv->ai_model,
                                                       (ai_model_detect_result_t *)srv->faces,
                                                       srv->face_num,
                                                       CAPTURE_WIDTH,
                                                       CAPTURE_HEIGHT,
                                                       raw_rgb_data,
                                                       result_rgb_data))
            {
                /* Save result image to SD card */
                if (srv->img_storage) 
                {
                    if(img_storage_save_jpeg(srv->img_storage, result_rgb_data) == IMG_STORAGE_OK)
                    {
                        LOG_I(MODULE_TAG "Face image saved to SD card");
                    }
                    else
                    {
                        LOG_E(MODULE_TAG "SD card image save failed");
                    }
                }
            }
        }
        led_base_turn_off(srv->s_led);

release_res:
        /* Release all DataBus resources (reference count compliant) */
        if (result_rgb_item)  { data_bus_release(result_rgb_item); }
        if (raw_rgb_item)     { data_bus_release(raw_rgb_item); }
        if (camera_item)      { data_bus_release(camera_item); }
        
        raw_rgb_item = camera_item = result_rgb_item = NULL;

        /* Publish AI processing completion event */
        event_bus_publish_simple(SYS_EVENT_BUS_NAME, EVENT_TYPE_FACE_PROCESS_DONE, MODULE_NAME);
    }

    LOG_I(MODULE_TAG "AI worker thread exited safely");
    return NULL;
}

// ==========================================================================
// Service Start Function
// ==========================================================================
/**
 * @brief   Start face detection service and real-time thread
 * @return  0 on success, negative value on failure
 * @pre     Service initialized successfully (face_srv_init)
 * @post    Worker thread running, event wakeup enabled
 * @thread_safety No, call only once
 */
static int face_srv_start(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    thread_err_t thread_ret;

    /* Initialize condition variable for event wakeup */
    int ret = pthread_cond_init(&srv->cond, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "Condition variable initialization failed");
        return -1;
    }

    /* Create real-time thread (auto name, stack, priority, CPU affinity) */
    thread_ret = thread_create_rt(&srv->work_thread,
                                  "FACE_Detect",
                                  FACE_THREAD_STACK_SIZE,
                                  face_work_thread,
                                  NULL,
                                  FACE_RT_PRIORITY,
                                  FACE_CPU_ID);

    if (thread_ret != THREAD_OK)
    {
        LOG_E(MODULE_TAG "Realtime thread creation failed, err=%d", thread_ret);
        pthread_cond_destroy(&srv->cond);
        return -1;
    }

    srv->is_paused = false;
    LOG_I(MODULE_TAG "Face detection service started [Priority=70 | 5FPS]");
    return 0;
}

// ==========================================================================
// Service Resource Cleanup
// ==========================================================================
/**
 * @brief   Full resource cleanup for face detection service
 * @details Safe thread stop, event unsubscription, resource destruction
 * @note    Atomic cleanup, no resource leakage
 * @pre     Service is running or paused
 * @post    All resources released, service stopped completely
 * @thread_safety No, called only on system stop/error
 */
static void face_srv_cleanup(void)
{
    face_detect_srv_t *srv = &s_face_srv;

    LOG_W(MODULE_TAG "Starting full resource release");

    /* 1. Stop worker thread safely */
    thread_stop(&srv->work_thread);
    srv->is_paused = true;

    /* Wake up blocked thread to exit */
    pthread_mutex_lock(&srv->mutex);
    pthread_cond_signal(&srv->cond);
    pthread_mutex_unlock(&srv->mutex);

    /* 2. Wait for thread exit */
    if (thread_is_running(&srv->work_thread))
    {
        thread_join(&srv->work_thread, NULL);
    }

    /* Unsubscribe from all event buses */
    if (srv->evt_sys_sub_id >= 0)
    {
        event_bus_unsubscribe(SYS_EVENT_BUS_NAME, srv->evt_sys_sub_id);
    }
    if (srv->evt_capture_sub_id >= 0)
    {
        event_bus_unsubscribe(CAPTURE_EVENT_BUS, srv->evt_capture_sub_id);
    }

    /* Destroy synchronization primitives */
    pthread_cond_destroy(&srv->cond);
    pthread_mutex_destroy(&srv->mutex);

    /* Destroy AI model and dual RGB buses */
    if (srv->ai_model)
    {
        ai_model_destroy(srv->ai_model);
        srv->ai_model = NULL;
    }
    data_bus_deinit(AI_RAW_RGB_DATA_BUS);
    data_bus_deinit(FACE_RESULT_RGB_DATA_BUS);

    /* Release SD card storage */
    if (srv->img_storage) {
        img_storage_deinit(srv->img_storage);
        srv->img_storage = NULL;
    }

    /* Release LED indicator */
    led_base_turn_off(srv->s_led);
    led_indicator_destroy(srv->s_led);

    LOG_I(MODULE_TAG "All resources released successfully");
}

// ==========================================================================
// Service Initialization
// ==========================================================================
/**
 * @brief   Initialize face detection service resources
 * @return  0 on success, negative value on failure
 * @details Initialize mutex, dual DataBus, AI model, event subscription, SD card, LED
 * @pre     System buses and drivers initialized
 * @post    Service ready to start on system resume event
 * @thread_safety No, called once during auto-init
 */
static int face_srv_init(void)
{
    face_detect_srv_t *srv = &s_face_srv;
    int ret = -1;

    /* Clear context structure */
    memset(srv, 0, sizeof(face_detect_srv_t));
    srv->evt_sys_sub_id = -1;
    srv->evt_capture_sub_id = -1;
    srv->frame_sample_cnt = 0;

    /* Initialize thread mutex */
    ret = pthread_mutex_init(&srv->mutex, NULL);
    if (ret != 0)
    {
        LOG_E(MODULE_TAG "Mutex initialization failed");
        return -1;
    }

    /* Initialize isolated raw RGB DataBus */
    data_bus_config_t ai_raw_rgb_cfg = {
        .name = AI_RAW_RGB_DATA_BUS,
        .max_item_size = AI_RAW_RGB_FRAME_SIZE,
        .max_items = AI_RAW_RGB_POOL_SIZE,
        .max_subscribers = AI_RAW_RGB_MAX_SUBSCRIBERS
    };
    ret = data_bus_init(&ai_raw_rgb_cfg);
    if (ret != DATA_BUS_OK)
    {
        LOG_E(MODULE_TAG "AI raw RGB bus initialization failed");
        pthread_mutex_destroy(&srv->mutex);
        return -1;
    }

    /* Initialize isolated face result RGB DataBus */
    data_bus_config_t face_result_rgb_cfg = {
        .name = FACE_RESULT_RGB_DATA_BUS,
        .max_item_size = FACE_RESULT_RGB_FRAME_SIZE,
        .max_items = FACE_RESULT_RGB_POOL_SIZE,
        .max_subscribers = FACE_RESULT_RGB_MAX_SUBSCRIBERS
    };
    ret = data_bus_init(&face_result_rgb_cfg);
    if (ret != DATA_BUS_OK)
    {
        LOG_E(MODULE_TAG "Face result RGB bus initialization failed");
        pthread_mutex_destroy(&srv->mutex);
        data_bus_deinit(AI_RAW_RGB_DATA_BUS);
        return -1;
    }

    /* Initialize AI model via factory (auto-selects MNN/RKNN based on platform) */
    ai_model_config_t ai_cfg = {
        .model_path    = AI_MODEL_PATH,
        .input_width   = AI_INPUT_WIDTH,
        .input_height  = AI_INPUT_HEIGHT,
        .score_thresh  = AI_SCORE_THRESH,
        .iou_thresh    = AI_IOU_THRESH
    };
    srv->ai_model = ai_model_factory_create(&ai_cfg);
    if (!srv->ai_model || ai_model_init(srv->ai_model) != AI_MODEL_OK)
    {
        LOG_E(MODULE_TAG "AI model initialization failed");
        data_bus_deinit(AI_RAW_RGB_DATA_BUS);
        data_bus_deinit(FACE_RESULT_RGB_DATA_BUS);
        pthread_mutex_destroy(&srv->mutex);
        return -1;
    }

    /* Subscribe to system event bus */
    event_subscriber_t sys_sub = {
        .event_type = EVENT_TYPE_INVALID,
        .callback = event_bus_cb,
        .user_data = srv,
        .skip_self_published = true
    };
    srv->evt_sys_sub_id = event_bus_subscribe(SYS_EVENT_BUS_NAME, &sys_sub);

    if (srv->evt_sys_sub_id < 0 ) 
    {
        LOG_E(MODULE_TAG "Event bus subscription failed");
        face_srv_cleanup();
        return -1;
    }

    /* Initialize SD card image storage */
    srv->img_storage = img_storage_init();
    if (srv->img_storage) {
        LOG_I(MODULE_TAG "SD card storage initialized successfully");
    } else {
        LOG_W(MODULE_TAG "SD card storage initialization failed, image save disabled");
    }

    /* Initialize LED indicator */
    srv->s_led = led_indicator_create("/dev/100ask_led0");
    if (srv->s_led) {
        led_base_init(srv->s_led);
    }

    LOG_I(MODULE_TAG "Face detection service initialized [5FPS + Dual Bus Isolation]");
    return 0;
}

// ==========================================================================
// Auto Initialization (System Init Call)
// ==========================================================================
/**
 * @brief   Auto-init entry for system service level
 * @return  0 on success, negative value on failure
 * @note    Registered via MODULE_INIT_LEVEL, auto-run on boot
 */
static int face_srv_auto_init(void)
{
    if (face_srv_init() != 0)
    {
        return -1;
    }
    LOG_I(MODULE_TAG "Module auto-load completed, waiting for system start command");
    return 0;
}

/* Register to system service initialization level */
MODULE_INIT_LEVEL(INIT_SERVICE, face_srv_auto_init);

/******************************* End of file **********************************/