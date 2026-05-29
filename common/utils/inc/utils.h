/**
 * @file    utils.h
 * @brief   Universal utility component for embedded Linux
 * @details Includes 5 categories of helper functions:
 *          Time, Endianness, String, Memory, Math
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */

#ifndef __UTILS_H
#define __UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// ==========================================================================
// Generic Macro Utilities
// ==========================================================================
/** Generic absolute value macro */
#define utils_abs_generic(x)   ((x) >= 0 ? (x) : -(x))
/** Generic maximum value macro */
#define utils_max_generic(a, b) ((a) > (b) ? (a) : (b))
/** Generic minimum value macro */
#define utils_min_generic(a, b) ((a) < (b) ? (a) : (b))

// ==========================================================================
// Time Utilities
// ==========================================================================
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief  Get current monotonic timestamp in microseconds
 * @return Timestamp (us)
 */
uint64_t utils_get_timestamp_us(void);

/**
 * @brief  Get current monotonic timestamp in milliseconds
 * @return Timestamp (ms)
 */
uint64_t utils_get_timestamp_ms(void);

/**
 * @brief  Format timestamp to human-readable string
 * @param  timestamp_us  Timestamp in microseconds
 * @param  buf           Output string buffer
 * @param  buf_size      Size of output buffer
 * @return Pointer to formatted string (buf)
 */
char* utils_format_timestamp(uint64_t timestamp_us, char *buf, size_t buf_size);

// ==========================================================================
// Endian Conversion Utilities
// ==========================================================================
/**
 * @brief  Convert 16-bit host byte order to network byte order (big-endian)
 * @param  host16  16-bit host value
 * @return 16-bit network value
 */
uint16_t utils_htons(uint16_t host16);

/**
 * @brief  Convert 16-bit network byte order to host byte order
 * @param  net16  16-bit network value
 * @return 16-bit host value
 */
uint16_t utils_ntohs(uint16_t net16);

/**
 * @brief  Convert 32-bit host byte order to network byte order
 * @param  host32  32-bit host value
 * @return 32-bit network value
 */
uint32_t utils_htonl(uint32_t host32);

/**
 * @brief  Convert 32-bit network byte order to host byte order
 * @param  net32  32-bit network value
 * @return 32-bit host value
 */
uint32_t utils_ntohl(uint32_t net32);

/**
 * @brief  Check if the system uses big-endian byte order
 * @return true = big-endian, false = little-endian
 */
bool utils_is_big_endian(void);

// ==========================================================================
// String Utilities
// ==========================================================================
/**
 * @brief  Safe string copy (guarantees null termination)
 * @param  dst        Destination buffer
 * @param  src        Source string
 * @param  dst_size   Size of destination buffer
 * @return Number of copied characters (excluding null terminator)
 */
size_t utils_strlcpy(char *dst, const char *src, size_t dst_size);

/**
 * @brief  Safe string concatenation (guarantees null termination)
 * @param  dst        Destination buffer
 * @param  src        Source string
 * @param  dst_size   Size of destination buffer
 * @return Total length of concatenated string
 */
size_t utils_strlcat(char *dst, const char *src, size_t dst_size);

/**
 * @brief  Trim leading and trailing whitespace from string (in-place)
 * @param  str  Input/output string
 * @return Pointer to trimmed string
 */
char* utils_trim(char *str);

/**
 * @brief  Safe string to integer conversion
 * @param  str      Input string
 * @param  out_val  Output integer value
 * @return 0 = success, non-zero = failure
 */
int utils_atoi_safe(const char *str, int *out_val);

/**
 * @brief  Safe string to 64-bit integer conversion
 * @param  str      Input string
 * @param  out_val  Output 64-bit integer value
 * @return 0 = success, non-zero = failure
 */
int utils_atoll_safe(const char *str, int64_t *out_val);

// ==========================================================================
// Memory Utilities
// ==========================================================================
/**
 * @brief  Safe memory zero initialization
 * @param  ptr   Memory pointer
 * @param  size  Size in bytes
 */
void utils_memzero(void *ptr, size_t size);

/**
 * @brief  Check if memory region is all zeros
 * @param  ptr   Memory pointer
 * @param  size  Size in bytes
 * @return true = all zeros
 */
bool utils_memiszero(const void *ptr, size_t size);

// ==========================================================================
// Math Utilities
// ==========================================================================
/**
 * @brief  Calculate absolute value (32-bit integer)
 * @param  x  Input value
 * @return Absolute value
 */
int32_t utils_abs(int32_t x);

/**
 * @brief  Calculate absolute value (64-bit integer)
 * @param  x  Input value
 * @return Absolute value
 */
int64_t utils_llabs(int64_t x);

/**
 * @brief  Calculate absolute value (float)
 * @param  x  Input value
 * @return Absolute value
 */
float    utils_fabsf(float x);

/**
 * @brief  Calculate absolute value (double)
 * @param  x  Input value
 * @return Absolute value
 */
double   utils_fabs(double x);

/**
 * @brief  Calculate maximum value (32-bit integer)
 * @param  a  Input value A
 * @param  b  Input value B
 * @return Maximum value
 */
int32_t utils_max(int32_t a, int32_t b);

/**
 * @brief  Calculate minimum value (32-bit integer)
 * @param  a  Input value A
 * @param  b  Input value B
 * @return Minimum value
 */
int32_t utils_min(int32_t a, int32_t b);

/**
 * @brief  Calculate maximum value (float)
 * @param  a  Input value A
 * @param  b  Input value B
 * @return Maximum value
 */
float    utils_fmaxf(float a, float b);

/**
 * @brief  Calculate minimum value (float)
 * @param  a  Input value A
 * @param  b  Input value B
 * @return Minimum value
 */
float    utils_fminf(float a, float b);

/**
 * @brief  Calculate maximum value (double)
 * @param  a  Input value A
 * @param  b  Input value B
 * @return Maximum value
 */
double   utils_fmax(double a, double b);

/**
 * @brief  Calculate minimum value (double)
 * @param  a  Input value A
 * @param  b  Input value B
 * @return Minimum value
 */
double   utils_fmin(double a, double b);

/**
 * @brief  Clamp value between min and max bounds
 * @param  val      Input value
 * @param  min_val  Minimum bound
 * @param  max_val  Maximum bound
 * @return Clamped value
 */
int32_t utils_clamp(int32_t val, int32_t min_val, int32_t max_val);

/**
 * @brief  Integer division with ceiling rounding
 * @param  a  Dividend
 * @param  b  Divisor
 * @return Ceiling division result
 */
uint32_t utils_div_ceil(uint32_t a, uint32_t b);

#ifdef __cplusplus
}
#endif
#endif /* __UTILS_H */