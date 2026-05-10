#ifndef __IMG_PROC_H__
#define __IMG_PROC_H__

#include <stdint.h>
#include <stddef.h>

/* 图像格式枚举 */
typedef enum {
    IMG_FMT_YUYV422 = 0, /* YUYV 4:2:2 交错格式 */
    IMG_FMT_RGB565,       /* RGB565 16位 */
    IMG_FMT_RGB888,       /* RGB888 24位（模型输入常用） */
    IMG_FMT_BGR888,       /* BGR888 24位（OpenCV常用） */
    IMG_FMT_GRAY8,         /* 灰度8位 */
    IMG_FMT_MAX
} img_format_t;

/* 图像缓冲区结构体 */
typedef struct {
    uint8_t *data;      /* 像素数据指针 */
    img_format_t format; /* 像素格式 */
    uint32_t width;     /* 宽度（像素） */
    uint32_t height;    /* 高度（像素） */
    uint32_t stride;    /* 行跨度（字节，用于对齐） */
    uint32_t size;      /* 总数据大小（字节） */
} img_buf_t;

/* 图像预处理操作函数指针集合 */
typedef struct {
    /* 格式转换：in -> out */
    int (*convert)(const img_buf_t *in, img_buf_t *out);
    
    /* 图像缩放：最近邻插值，in -> out */
    int (*resize)(const img_buf_t *in, img_buf_t *out);
    
    /* RGB归一化+张量排布：RGB888 -> 模型输入张量
     * norm_mean: 归一化均值（如 [127.0, 127.0, 127.0]）
     * norm_std:  归一化标准差（如 [128.0, 128.0, 128.0]）
     * layout:    张量排布 ("NHWC" 或 "NCHW")
     */
    int (*normalize)(const img_buf_t *in, float *out, 
                     const float *norm_mean, const float *norm_std,
                     const char *layout);
} img_ops_t;

/* 注册图像预处理实现（纯C/OpenCV通过此函数切换） */
int img_proc_register(const img_ops_t *ops);

/* 获取已注册的图像预处理操作 */
const img_ops_t *img_proc_get_ops(void);

/* 辅助函数：计算图像缓冲区大小 */
uint32_t img_proc_calc_size(img_format_t fmt, uint32_t width, uint32_t height);

/* 辅助函数：初始化图像缓冲区（内部不分配内存，由调用者管理） */
void img_buf_init(img_buf_t *buf, uint8_t *data, img_format_t fmt, 
                  uint32_t width, uint32_t height);

#endif /* __IMG_PROC_H__ */
