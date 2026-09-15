#include "mask/common.h"

#include <stdarg.h>
#include <time.h>

static const char *level_name(mask_log_level_t level) {
    switch (level) {
        case MASK_LOG_DEBUG: return "DEBUG";
        case MASK_LOG_INFO:  return "INFO";
        case MASK_LOG_WARN:  return "WARN";
        case MASK_LOG_ERROR: return "ERROR";
        default: return "?";
    }
}

void mask_log(mask_log_level_t level, const char *fmt, ...) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    FILE *out = (level >= MASK_LOG_WARN) ? stderr : stdout;
    fprintf(out, "[%ld.%03ld] [%s] ", (long)ts.tv_sec, ts.tv_nsec / 1000000L, level_name(level));

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fputc('\n', out);
}
