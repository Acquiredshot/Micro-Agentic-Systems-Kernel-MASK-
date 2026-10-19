#include "mask/common.h"
#include "mask/config.h"

#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "cJSON.h"

/* ---- logging ---- */

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

/* ---- event envelope ---- */

/*
 * Wraps a tool's raw payload into a standard event envelope so downstream
 * consumers (Event Fabric, Security Graph, cross-app correlators) can parse
 * every MASK event without per-tool knowledge.
 *
 * envelope = {
 *   "timestamp_ms": <uint64>,
 *   "asset_id":    <string>,
 *   "source":      "MASK",
 *   "event_type":  <string, e.g. "sysinfo", "net_connections", "proc_list">,
 *   "severity":    <string, "info"|"warning"|"critical">,
 *   "payload":     <arbitrary JSON object from the tool>
 * }
 *
 * severity is left as "info" by the caller; individual tools that detect
 * something unusual can override it when they build their payload.
 */
char *mask_event_envelope(uint64_t timestamp_ms, const char *asset_id,
                          const char *event_type, const char *severity,
                          const char *payload_json) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    char ts_buf[32];
    snprintf(ts_buf, sizeof(ts_buf), "%" PRIu64, timestamp_ms);
    cJSON_AddStringToObject(root, "timestamp_ms", ts_buf);
    cJSON_AddStringToObject(root, "asset_id", asset_id ? asset_id : "unknown");
    cJSON_AddStringToObject(root, "source", "MASK");
    cJSON_AddStringToObject(root, "event_type", event_type ? event_type : "unknown");
    cJSON_AddStringToObject(root, "severity", severity ? severity : "info");

    if (payload_json) {
        cJSON *payload = cJSON_Parse(payload_json);
        if (payload) {
            cJSON_AddItemToObject(root, "payload", payload);
        } else {
            cJSON_AddStringToObject(root, "payload", payload_json);
        }
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* ---- per-host event log ---- */

int mask_event_log_open(struct mask_config *cfg) {
    if (!cfg || !cfg->event_log_path[0]) {
        return -1;
    }
    int fd = open(cfg->event_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        MASK_LOGW("event log: cannot open %s: %s", cfg->event_log_path, strerror(errno));
        return -1;
    }
    MASK_LOGI("event log: writing to %s", cfg->event_log_path);
    return fd;
}

void mask_event_log_write(int fd, const char *json_line) {
    if (fd < 0 || !json_line) return;
    size_t len = strlen(json_line);
    if (len == 0) return;
    /* Write JSON line + newline atomically-ish; best-effort. */
    ssize_t w = write(fd, json_line, len);
    if (w >= 0) {
        w = write(fd, "\n", 1);
    }
    if (w < 0) {
        MASK_LOGW("event log: write failed: %s", strerror(errno));
    }
}

void mask_event_log_close(int *fd) {
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}
