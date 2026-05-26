#ifndef __UTILS_H
#define __UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
// 时间、字节序、字符串、内存、数学五大类工具
// ==========================================================================

// 通用绝对值宏
#define utils_abs_generic(x)   ((x) >= 0 ? (x) : -(x))

// 通用最大/最小值宏
#define utils_max_generic(a, b) ((a) > (b) ? (a) : (b))
#define utils_min_generic(a, b) ((a) < (b) ? (a) : (b))

// 时间工具函数
// ==========================================================================
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 获取当前时间戳（微秒）
 * @return 时间戳（微秒）
 */
uint64_t utils_get_timestamp_us(void);

/**
 * @brief 获取当前时间戳（毫秒）
 * @return 时间戳（毫秒）
 */
uint64_t utils_get_timestamp_ms(void);

/**
 * @brief 格式化时间戳为字符串
 * @param timestamp_us 时间戳（微秒）
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 格式化后的字符串（buf指针）
 */
char* utils_format_timestamp(uint64_t timestamp_us, char *buf, size_t buf_size);

// ==========================================================================
// 字节序转换工具函数
// ==========================================================================

/**
 * @brief 16位主机序转网络序（大端）
 */
uint16_t utils_htons(uint16_t host16);

/**
 * @brief 16位网络序转主机序
 */
uint16_t utils_ntohs(uint16_t net16);

/**
 * @brief 32位主机序转网络序
 */
uint32_t utils_htonl(uint32_t host32);

/**
 * @brief 32位网络序转主机序
 */
uint32_t utils_ntohl(uint32_t net32);

/**
 * @brief 检查系统是否为大端序
 * @return true=大端序
 */
bool utils_is_big_endian(void);

// ==========================================================================
// 字符串工具函数
// ==========================================================================

/**
 * @brief 安全的字符串拷贝（保证零终止）
 * @param dst 目标缓冲区
 * @param src 源字符串
 * @param dst_size 目标缓冲区大小
 * @return 实际拷贝的字符数（不包括零终止符）
 */
size_t utils_strlcpy(char *dst, const char *src, size_t dst_size);

/**
 * @brief 安全的字符串拼接（保证零终止）
 * @param dst 目标缓冲区
 * @param src 源字符串
 * @param dst_size 目标缓冲区大小
 * @return 实际拼接后的总长度
 */
size_t utils_strlcat(char *dst, const char *src, size_t dst_size);

/**
 * @brief 去除字符串首尾空白字符
 * @param str 输入/输出字符串（原地修改）
 * @return 处理后的字符串指针
 */
char* utils_trim(char *str);

/**
 * @brief 字符串转整数（安全版本）
 * @param str 输入字符串
 * @param out_val 输出整数值
 * @return 0=成功，非0=失败
 */
int utils_atoi_safe(const char *str, int *out_val);

/**
 * @brief 字符串转64位整数（安全版本）
 * @param str 输入字符串
 * @param out_val 输出整数值
 * @return 0=成功，非0=失败
 */
int utils_atoll_safe(const char *str, int64_t *out_val);

// ==========================================================================
// 内存工具函数
// ==========================================================================

/**
 * @brief 安全的内存清零
 * @param ptr 内存指针
 * @param size 大小（字节）
 */
void utils_memzero(void *ptr, size_t size);

/**
 * @brief 检查内存区域是否全零
 * @param ptr 内存指针
 * @param size 大小（字节）
 * @return true=全零
 */
bool utils_memiszero(const void *ptr, size_t size);

// ==========================================================================
// 数学工具函数
// ==========================================================================

/**
 * @brief 计算绝对值（32位）
 */
int32_t utils_abs(int32_t x);

/**
 * @brief 计算绝对值（64位）
 */
int64_t utils_llabs(int64_t x);
// 浮点绝对值（新增！）
float    utils_fabsf(float x);
double   utils_fabs(double x);
/**
 * @brief 计算两个数的最大值（32位）
 */
int32_t utils_max(int32_t a, int32_t b);

/**
 * @brief 计算两个数的最小值（32位）
 */
int32_t utils_min(int32_t a, int32_t b);
// 浮点最大/最小（新增！修复你边界BUG）
float    utils_fmaxf(float a, float b);
float    utils_fminf(float a, float b);
double   utils_fmax(double a, double b);
double   utils_fmin(double a, double b);
/**
 * @brief 数值钳制（限制在 min 和 max 之间）
 */
int32_t utils_clamp(int32_t val, int32_t min_val, int32_t max_val);

/**
 * @brief 除法向上取整
 */
uint32_t utils_div_ceil(uint32_t a, uint32_t b);
#ifdef __cplusplus
}
#endif
#endif /* __UTILS_H */