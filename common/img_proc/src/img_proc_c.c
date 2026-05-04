#include "img_proc.h"
#include <string.h>
#include <math.h>

/* -------------------------- 内部辅助宏与LUT -------------------------- */
#define CLIP_U8(x) ((x) > 255 ? 255 : ((x) < 0 ? 0 : (x)))

/* YUV422转RGB888的系数（BT.601标准，SDTV） */
#define YUV2RGB_COEFF_Y(y)  ((y) - 16)
#define YUV2RGB_COEFF_U(u)  ((u) - 128)
#define YUV2RGB_COEFF_V(v)  ((v) - 128)

#define R_FROM_YUV(y, v)  CLIP_U8((298 * YUV2RGB_COEFF_Y(y) + 409 * YUV2RGB_COEFF_V(v) + 128) >> 8)
#define G_FROM_YUV(y, u, v) CLIP_U8((298 * YUV2RGB_COEFF_Y(y) - 100 * YUV2RGB_COEFF_U(u) - 208 * YUV2RGB_COEFF_V(v) + 128) >> 8)
#define B_FROM_YUV(y, u)  CLIP_U8((298 * YUV2RGB_COEFF_Y(y) + 516 * YUV2RGB_COEFF_U(u) + 128) >> 8)

/* -------------------------- 纯C格式转换实现 -------------------------- */
static int _cvt_yuyv2rgb565(const img_buf_t *in, img_buf_t *out)
{
    if (!in || !out || in->format != IMG_FMT_YUYV422 || out->format != IMG_FMT_RGB565)
        return -1;
    if (in->width != out->width || in->height != out->height)
        return -1;

    const uint8_t *src = in->data;
    uint16_t *dst = (uint16_t *)out->data;
    uint32_t pixel_cnt = in->width * in->height;

    for (uint32_t i = 0; i < pixel_cnt; i += 2) {
        /* YUYV数据包：Y0 U0 Y1 V0 */
        uint8_t y0 = src[0];
        uint8_t u0 = src[1];
        uint8_t y1 = src[2];
        uint8_t v0 = src[3];
        src += 4;

        /* 第一个像素：Y0 U0 V0 */
        int r = R_FROM_YUV(y0, v0);
        int g = G_FROM_YUV(y0, u0, v0);
        int b = B_FROM_YUV(y0, u0);
        /* RGB888 -> RGB565 */
        *dst++ = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

        /* 第二个像素：Y1 U0 V0 */
        r = R_FROM_YUV(y1, v0);
        g = G_FROM_YUV(y1, u0, v0);
        b = B_FROM_YUV(y1, u0);
        *dst++ = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    return 0;
}

static int _cvt_yuyv2rgb888(const img_buf_t *in, img_buf_t *out)
{
    if (!in || !out || in->format != IMG_FMT_YUYV422 || out->format != IMG_FMT_RGB888)
        return -1;
    if (in->width != out->width || in->height != out->height)
        return -1;

    const uint8_t *src = in->data;
    uint8_t *dst = out->data;
    uint32_t pixel_cnt = in->width * in->height;

    for (uint32_t i = 0; i < pixel_cnt; i += 2) {
        uint8_t y0 = src[0];
        uint8_t u0 = src[1];
        uint8_t y1 = src[2];
        uint8_t v0 = src[3];
        src += 4;

        /* 像素1 */
        *dst++ = R_FROM_YUV(y0, v0);
        *dst++ = G_FROM_YUV(y0, u0, v0);
        *dst++ = B_FROM_YUV(y0, u0);
        /* 像素2 */
        *dst++ = R_FROM_YUV(y1, v0);
        *dst++ = G_FROM_YUV(y1, u0, v0);
        *dst++ = B_FROM_YUV(y1, u0);
    }
    return 0;
}

static int _c_convert(const img_buf_t *in, img_buf_t *out)
{
    if (!in || !out || !in->data || !out->data)
        return -1;

    /* 根据输入输出格式分发 */
    if (in->format == IMG_FMT_YUYV422 && out->format == IMG_FMT_RGB565)
        return _cvt_yuyv2rgb565(in, out);
    else if (in->format == IMG_FMT_YUYV422 && out->format == IMG_FMT_RGB888)
        return _cvt_yuyv2rgb888(in, out);
    else
        return -1; /* 其他格式暂未实现，按需添加 */
}

/* -------------------------- 纯C最近邻缩放实现 -------------------------- */
static int _c_resize(const img_buf_t *in, img_buf_t *out)
{
    if (!in || !out || !in->data || !out->data)
        return -1;
    if (in->format != out->format)
        return -1; /* 仅支持同格式缩放 */

    uint32_t src_w = in->width;
    uint32_t src_h = in->height;
    uint32_t dst_w = out->width;
    uint32_t dst_h = out->height;
    const uint8_t *src = in->data;
    uint8_t *dst = out->data;

    /* 计算缩放比例（定点数，避免浮点运算） */
    uint32_t scale_x = (src_w << 16) / dst_w;
    uint32_t scale_y = (src_h << 16) / dst_h;

    /* 根据像素格式处理 */
    if (in->format == IMG_FMT_RGB888) {
        for (uint32_t y = 0; y < dst_h; y++) {
            uint32_t src_y = (y * scale_y) >> 16;
            src_y = (src_y >= src_h) ? (src_h - 1) : src_y;

            for (uint32_t x = 0; x < dst_w; x++) {
                uint32_t src_x = (x * scale_x) >> 16;
                src_x = (src_x >= src_w) ? (src_w - 1) : src_x;

                /* 复制RGB888三个字节 */
                const uint8_t *src_pix = src + (src_y * src_w + src_x) * 3;
                uint8_t *dst_pix = dst + (y * dst_w + x) * 3;
                dst_pix[0] = src_pix[0];
                dst_pix[1] = src_pix[1];
                dst_pix[2] = src_pix[2];
            }
        }
        return 0;
    } else if (in->format == IMG_FMT_RGB565) {
        for (uint32_t y = 0; y < dst_h; y++) {
            uint32_t src_y = (y * scale_y) >> 16;
            src_y = (src_y >= src_h) ? (src_h - 1) : src_y;

            for (uint32_t x = 0; x < dst_w; x++) {
                uint32_t src_x = (x * scale_x) >> 16;
                src_x = (src_x >= src_w) ? (src_w - 1) : src_x;

                /* 复制RGB565两个字节 */
                const uint16_t *src_pix = (const uint16_t *)src + (src_y * src_w + src_x);
                uint16_t *dst_pix = (uint16_t *)dst + (y * dst_w + x);
                *dst_pix = *src_pix;
            }
        }
        return 0;
    } else {
        return -1; /* 其他格式暂未实现 */
    }
}

/* -------------------------- 纯C归一化+张量排布实现（RFB-320专用） -------------------------- */
/* RFB-320默认输入：320x240 RGB888，归一化 mean=[127,127,127], std=[128,128,128]，排布 NHWC */
static int _c_normalize(const img_buf_t *in, float *out, 
                        const float *norm_mean, const float *norm_std,
                        const char *layout)
{
    if (!in || !out || !norm_mean || !norm_std || !layout)
        return -1;
    if (in->format != IMG_FMT_RGB888)
        return -1;

    uint32_t w = in->width;
    uint32_t h = in->height;
    const uint8_t *src = in->data;

    /* 简单字符串比较，支持 NHWC 和 NCHW */
    int is_nhwc = (layout[0] == 'N' && layout[1] == 'H' && layout[2] == 'W' && layout[3] == 'C');
    int is_nchw = (layout[0] == 'N' && layout[1] == 'C' && layout[2] == 'H' && layout[3] == 'W');

    if (!is_nhwc && !is_nchw)
        return -1;

    if (is_nhwc) {
        /* NHWC排布：[H, W, C] */
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint32_t idx = (y * w + x) * 3;
                float r = (src[idx + 0] - norm_mean[0]) / norm_std[0];
                float g = (src[idx + 1] - norm_mean[1]) / norm_std[1];
                float b = (src[idx + 2] - norm_mean[2]) / norm_std[2];

                uint32_t out_idx = (y * w + x) * 3;
                out[out_idx + 0] = r;
                out[out_idx + 1] = g;
                out[out_idx + 2] = b;
            }
        }
    } else {
        /* NCHW排布：[C, H, W] */
        for (uint32_t c = 0; c < 3; c++) {
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    uint32_t src_idx = (y * w + x) * 3 + c;
                    float val = (src[src_idx] - norm_mean[c]) / norm_std[c];

                    uint32_t out_idx = c * h * w + y * w + x;
                    out[out_idx] = val;
                }
            }
        }
    }
    return 0;
}

/* -------------------------- 注册与辅助函数实现 -------------------------- */
static img_ops_t g_c_img_ops = {
    .convert = _c_convert,
    .resize = _c_resize,
    .normalize = _c_normalize,
};

static const img_ops_t *g_registered_ops = NULL;

int img_proc_register(const img_ops_t *ops)
{
    if (!ops)
        return -1;
    g_registered_ops = ops;
    return 0;
}

const img_ops_t *img_proc_get_ops(void)
{
    return g_registered_ops;
}

uint32_t img_proc_calc_size(img_format_t fmt, uint32_t width, uint32_t height)
{
    switch (fmt) {
        case IMG_FMT_YUYV422: return width * height * 2;
        case IMG_FMT_RGB565:   return width * height * 2;
        case IMG_FMT_RGB888:   return width * height * 3;
        case IMG_FMT_BGR888:   return width * height * 3;
        case IMG_FMT_GRAY8:     return width * height;
        default: return 0;
    }
}

void img_buf_init(img_buf_t *buf, uint8_t *data, img_format_t fmt, 
                  uint32_t width, uint32_t height)
{
    if (!buf)
        return;
    buf->data = data;
    buf->format = fmt;
    buf->width = width;
    buf->height = height;
    buf->stride = width * (fmt == IMG_FMT_RGB888 ? 3 : (fmt == IMG_FMT_GRAY8 ? 1 : 2));
    buf->size = img_proc_calc_size(fmt, width, height);
}

/* 模块初始化：默认注册纯C实现 */
void img_proc_c_init(void)
{
    img_proc_register(&g_c_img_ops);
}
