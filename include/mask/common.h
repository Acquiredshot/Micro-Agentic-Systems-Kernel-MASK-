#ifndef MASK_COMMON_H
#define MASK_COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define MASK_OK 0
#define MASK_ERR (-1)

typedef enum {
    MASK_LOG_DEBUG = 0,
    MASK_LOG_INFO,
    MASK_LOG_WARN,
    MASK_LOG_ERROR
} mask_log_level_t;

void mask_log(mask_log_level_t level, const char *fmt, ...);

#define MASK_LOGD(...) mask_log(MASK_LOG_DEBUG, __VA_ARGS__)
#define MASK_LOGI(...) mask_log(MASK_LOG_INFO, __VA_ARGS__)
#define MASK_LOGW(...) mask_log(MASK_LOG_WARN, __VA_ARGS__)
#define MASK_LOGE(...) mask_log(MASK_LOG_ERROR, __VA_ARGS__)

static inline uint64_t mask_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

#endif /* MASK_COMMON_H */
