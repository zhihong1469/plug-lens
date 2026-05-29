/**
 * @file    utils.c
 * @brief   Implementation of universal utility functions
 * @author  LuoZhihong
 * @github  https://github.com/zhihong1469/plug-lens
 * @date    2026-05-29
 * @version v1.0.0
 * @license MIT License
 */
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <arpa/inet.h>

// ==========================================================================
// Time Utilities Implementation
// ==========================================================================
uint64_t utils_get_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

uint64_t utils_get_timestamp_ms(void)
{
    return utils_get_timestamp_us() / 1000;
}

char* utils_format_timestamp(uint64_t timestamp_us, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0) {
        return NULL;
    }

    time_t sec = (time_t)(timestamp_us / 1000000);
    uint32_t us = (uint32_t)(timestamp_us % 1000000);

    struct tm tm;
    localtime_r(&sec, &tm);

    strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm);
    
    size_t len = strlen(buf);
    if (len + 8 < buf_size) {
        snprintf(buf + len, buf_size - len, ".%06u", us);
    }

    return buf;
}

// ==========================================================================
// Endian Conversion Implementation
// ==========================================================================
uint16_t utils_htons(uint16_t host16)
{
    return htons(host16);
}

uint16_t utils_ntohs(uint16_t net16)
{
    return ntohs(net16);
}

uint32_t utils_htonl(uint32_t host32)
{
    return htonl(host32);
}

uint32_t utils_ntohl(uint32_t net32)
{
    return ntohl(net32);
}

bool utils_is_big_endian(void)
{
    union {
        uint32_t i;
        char c[4];
    } u = {0x01020304};
    return u.c[0] == 1;
}

// ==========================================================================
// String Utilities Implementation
// ==========================================================================
size_t utils_strlcpy(char *dst, const char *src, size_t dst_size)
{
    if (dst == NULL || src == NULL || dst_size == 0) {
        return 0;
    }

    size_t src_len = strlen(src);
    size_t copy_len = (src_len < dst_size - 1) ? src_len : dst_size - 1;

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

    return copy_len;
}

size_t utils_strlcat(char *dst, const char *src, size_t dst_size)
{
    if (dst == NULL || src == NULL || dst_size == 0) {
        return 0;
    }

    size_t dst_len = strlen(dst);
    size_t src_len = strlen(src);
    size_t available = dst_size - dst_len - 1;

    if (available > 0) {
        size_t copy_len = (src_len < available) ? src_len : available;
        memcpy(dst + dst_len, src, copy_len);
        dst[dst_len + copy_len] = '\0';
    }

    return dst_len + src_len;
}

char* utils_trim(char *str)
{
    if (str == NULL) {
        return NULL;
    }

    // Trim leading whitespace
    char *start = str;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    // Trim trailing whitespace
    if (*start != '\0') {
        char *end = start + strlen(start) - 1;
        while (end > start && isspace((unsigned char)*end)) {
            end--;
        }
        *(end + 1) = '\0';
    }

    // Move trimmed string to start if shifted
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    return str;
}

int utils_atoi_safe(const char *str, int *out_val)
{
    if (str == NULL || out_val == NULL) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    
    long val = strtol(str, &endptr, 10);
    
    if (endptr == str || *endptr != '\0' || errno != 0 ||
        val < INT_MIN || val > INT_MAX) {
        return -1;
    }

    *out_val = (int)val;
    return 0;
}

int utils_atoll_safe(const char *str, int64_t *out_val)
{
    if (str == NULL || out_val == NULL) {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    
    long long val = strtoll(str, &endptr, 10);
    
    if (endptr == str || *endptr != '\0' || errno != 0) {
        return -1;
    }

    *out_val = (int64_t)val;
    return 0;
}

// ==========================================================================
// Memory Utilities Implementation
// ==========================================================================
void utils_memzero(void *ptr, size_t size)
{
    if (ptr == NULL || size == 0) {
        return;
    }
    memset(ptr, 0, size);
}

bool utils_memiszero(const void *ptr, size_t size)
{
    if (ptr == NULL || size == 0) {
        return true;
    }

    const uint8_t *p = (const uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (p[i] != 0) {
            return false;
        }
    }
    return true;
}

// ==========================================================================
// Integer Math Implementation
// ==========================================================================
int32_t utils_abs(int32_t x)
{
    return (x < 0) ? -x : x;
}

int64_t utils_llabs(int64_t x)
{
    return (x < 0) ? -x : x;
}

int32_t utils_max(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

int32_t utils_min(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

// ==========================================================================
// Floating-Point Math Implementation
// ==========================================================================
float utils_fabsf(float x)
{
    return (x < 0.0f) ? -x : x;
}

double utils_fabs(double x)
{
    return (x < 0.0) ? -x : x;
}

float utils_fmaxf(float a, float b)
{
    return (a > b) ? a : b;
}

float utils_fminf(float a, float b)
{
    return (a < b) ? a : b;
}

double utils_fmax(double a, double b)
{
    return (a > b) ? a : b;
}

double utils_fmin(double a, double b)
{
    return (a < b) ? a : b;
}

int32_t utils_clamp(int32_t val, int32_t min_val, int32_t max_val)
{
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

uint32_t utils_div_ceil(uint32_t a, uint32_t b)
{
    if (b == 0) {
        return 0;
    }
    return (a + b - 1) / b;
}