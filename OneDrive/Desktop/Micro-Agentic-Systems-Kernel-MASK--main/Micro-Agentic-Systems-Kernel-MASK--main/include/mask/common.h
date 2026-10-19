#ifndef MASK_COMMON_H
#define MASK_COMMON_H

#include "mask/config.h"
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <inttypes.h>

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

/** @brief Wraps a tool's raw payload into a standard event envelope.
 */
char *mask_event_envelope(uint64_t timestamp_ms, const char *asset_id,
                          const char *event_type, const char *severity,
                          const char *payload_json);

/** @brief Opens (or creates) the per-host event log file. The path comes from
 *        config.event_log_path. Returns a file descriptor, or -1 if the path
 *        is unset or the file can't be opened.
 */
int mask_event_log_open(struct mask_config *cfg);

/** @brief Appends a JSON line to the per-host event log (if open).
 */
void mask_event_log_write(int fd, const char *json_line);

/** @brief Closes the per-host event log if open.
 */
void mask_event_log_close(int *fd);

#endif /* MASK_COMMON_H */
