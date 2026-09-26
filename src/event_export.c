#include "mask/common.h"
#include "mask/config.h"
#include "mask/ipc.h"
#include "mask/ring_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <curl/curl.h>

#include "cJSON.h"

/* ---- HTTP event export ---- */

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

/**
 * @brief POSTs a batch of JSON event lines to the configured export URL.
 *        Runs on the export thread; best-effort, logs warnings on failure.
 */
static void export_batch(const char *url, const char *asset_id,
                         char **lines, size_t count) {
    if (!url || !url[0] || count == 0) return;

    /* Build the export payload: {"asset_id":"...","events":[...]} */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "asset_id", asset_id);

    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        cJSON *parsed = cJSON_Parse(lines[i]);
        if (parsed) {
            cJSON_AddItemToArray(arr, parsed); /* transfers ownership */
        } else {
            cJSON *raw = cJSON_CreateString(lines[i]);
            cJSON_AddItemToArray(arr, raw);
        }
    }
    cJSON_AddItemToObject(root, "events", arr);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body) return;

    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            MASK_LOGW("event export: POST failed: %s", curl_easy_strerror(res));
        } else {
            long code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            MASK_LOGD("event export: POSTed %zu events, HTTP %ld", count, code);
        }
        curl_easy_cleanup(curl);
    }
    free(body);
}

struct export_ctx {
    char **lines;
    size_t count;
    size_t capacity;
    pthread_mutex_t lock;
};

static void *export_thread_main(void *arg) {
    struct mask_ipc_context *ctx = (struct mask_ipc_context *)arg;
    const char *url = ctx->cfg->event_export_url;
    const char *asset_id = ctx->cfg->asset_id;

    if (!url || !url[0]) {
        MASK_LOGI("event export: no URL configured, exiting");
        return NULL;
    }

    /* curl_global_init is thread-safe and only needs to be called once.
     * It's called in llm_client_global_init already, but be defensive. */
    curl_global_init(CURL_GLOBAL_ALL);

    FILE *f = fopen(ctx->cfg->event_log_path, "r");
    if (!f) {
        MASK_LOGW("event export: cannot open event log %s: %s",
                   ctx->cfg->event_log_path, strerror(errno));
        curl_global_cleanup();
        return NULL;
    }

    /* Seek to end — only export new events going forward */
    fseek(f, 0, SEEK_END);

    char **lines = NULL;
    size_t count = 0;
    size_t capacity = 0;
    char buf[4096];

    while (1) {
        ssize_t n = fread(buf, 1, sizeof(buf) - 1, f);
        if (n > 0) {
            buf[n] = '\0';
            /* Split on newlines; each line is a complete JSON event */
            char *start = buf;
            char *end = buf + n;
            while (start < end) {
                char *nl = memchr(start, '\n', end - start);
                size_t line_len = nl ? (size_t)(nl - start) : (size_t)(end - start);
                if (line_len == 0) {
                    start = nl ? nl + 1 : end;
                    continue;
                }
                if (count >= capacity) {
                    size_t new_cap = capacity == 0 ? 16 : capacity * 2;
                    char **tmp = realloc(lines, new_cap * sizeof(char *));
                    if (!tmp) break;
                    lines = tmp;
                    capacity = new_cap;
                }
                char *line = strndup(start, line_len);
                if (line) {
                    lines[count++] = line;
                }
                start = nl ? nl + 1 : end;
            }
        }
        if (count > 0) {
            export_batch(url, asset_id, lines, count);
            for (size_t i = 0; i < count; i++) {
                free(lines[i]);
            }
            count = 0;
        }
        if (feof(f)) {
            /* No new data; sleep and retry */
            sleep(10);
            clearerr(f);
        } else {
            /* Error or EOF from read error */
            sleep(5);
            clearerr(f);
        }
    }

    /* Unreachable in practice (infinite loop), but clean up on exit */
    for (size_t i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
    fclose(f);
    curl_global_cleanup();
}

void mask_event_export_start(struct mask_ipc_context *ctx) {
    pthread_t thr;
    if (pthread_create(&thr, NULL, (void *(*)(void *))export_thread_main, ctx) == 0) {
        pthread_detach(thr);
        MASK_LOGI("event export: started, URL=%s", ctx->cfg->event_export_url);
    } else {
        MASK_LOGE("event export: failed to start thread");
    }
}

/* ---- threat intel feed ---- */

struct threat_feed_ctx {
    struct mask_ipc_context *ctx;
    char *buf;
    size_t len;
};

static size_t threat_curl_write_cb(void *ptr, size_t size, size_t nmemb,
                                    void *userdata) {
    struct threat_feed_ctx *tf = (struct threat_feed_ctx *)userdata;
    size_t realsize = size * nmemb;
    char *tmp = realloc(tf->buf, tf->len + realsize + 1);
    if (!tmp) return 0;
    tf->buf = tmp;
    memcpy(tf->buf + tf->len, ptr, realsize);
    tf->len += realsize;
    tf->buf[tf->len] = '\0';
    return realsize;
}

static void *threat_feed_fetch_thread(void *arg) {
    struct threat_feed_ctx *tf = (struct threat_feed_ctx *)arg;
    const char *url = tf->ctx->cfg->threat_feed_url;
    if (!url || !url[0]) return NULL;

    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, threat_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, tf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        MASK_LOGW("threat feed: fetch failed: %s", curl_easy_strerror(res));
        free(tf->buf);
        return NULL;
    }

    /* Parse the response. Expected format: one IOC per line, or a JSON
     * array of strings. We'll handle both. */
    char *ioc_text = NULL;
    cJSON *parsed = cJSON_Parse(tf->buf);
    if (parsed) {
        /* JSON array of strings */
        if (cJSON_IsArray(parsed)) {
            cJSON *arr = parsed;
            size_t n = cJSON_GetArraySize(arr);
            if (n > 0) {
                size_t buf_size = n * 256 + 1;
                char *buf = malloc(buf_size);
                if (buf) {
                    buf[0] = '\0';
                    for (size_t i = 0; i < n; i++) {
                        cJSON *item = cJSON_GetArrayItem(arr, i);
                        if (cJSON_IsString(item) && item->valuestring) {
                            if (buf[0]) strcat(buf, "\n");
                            strncat(buf, item->valuestring, buf_size - strlen(buf) - 1);
                        }
                    }
                    ioc_text = buf;
                }
            }
        }
        cJSON_Delete(parsed);
    }
    if (!ioc_text) {
        /* Treat as plain text: one IOC per line */
        ioc_text = strdup(tf->buf);
    }
    free(tf->buf);

    if (ioc_text && ioc_text[0]) {
        size_t len = strlen(ioc_text);
        if (len < MASK_CFG_IOC_MAX) {
            snprintf(tf->ctx->cfg->ioc_data, sizeof(tf->ctx->cfg->ioc_data), "%s", ioc_text);
            MASK_LOGI("threat feed: updated IOC data (%zu bytes)", len);
        } else {
            MASK_LOGW("threat feed: IOC data too large (%zu bytes, max %d)", len, MASK_CFG_IOC_MAX);
        }
    }
    free(ioc_text);
    free(tf);
    return NULL;
}

void mask_threat_feed_trigger(struct mask_ipc_context *ctx) {
    struct threat_feed_ctx *tf = calloc(1, sizeof(*tf));
    if (!tf) return;
    tf->ctx = ctx;
    tf->buf = NULL;
    tf->len = 0;
    pthread_t thr;
    if (pthread_create(&thr, NULL, threat_feed_fetch_thread, tf) == 0) {
        pthread_detach(thr);
    } else {
        free(tf->buf);
        free(tf);
    }
}

/* ---- query_events (cross-app correlation) ---- */

struct query_filter {
    char asset_id[MASK_CFG_ASSET_ID_MAX];
    char event_type[64];
    uint64_t start_ts;
    uint64_t end_ts;
};

static int parse_query_filter(cJSON *filter_obj, struct query_filter *f) {
    memset(f, 0, sizeof(*f));
    f->start_ts = 0;
    f->end_ts = UINT64_MAX;

    if (!cJSON_IsObject(filter_obj)) return 0;

    cJSON *aid = cJSON_GetObjectItemCaseSensitive(filter_obj, "asset_id");
    if (cJSON_IsString(aid) && aid->valuestring) {
        snprintf(f->asset_id, sizeof(f->asset_id), "%s", aid->valuestring);
    }
    cJSON *et = cJSON_GetObjectItemCaseSensitive(filter_obj, "event_type");
    if (cJSON_IsString(et) && et->valuestring) {
        snprintf(f->event_type, sizeof(f->event_type), "%s", et->valuestring);
    }
    cJSON *sts = cJSON_GetObjectItemCaseSensitive(filter_obj, "start_ts");
    if (cJSON_IsNumber(sts)) f->start_ts = (uint64_t)sts->valuedouble;
    cJSON *ets = cJSON_GetObjectItemCaseSensitive(filter_obj, "end_ts");
    if (cJSON_IsNumber(ets)) f->end_ts = (uint64_t)ets->valuedouble;

    return 1;
}

/** @brief Returns ring buffer entries matching a query filter, as JSON.
 *        Used by Network Guardian and PAKSHIELD to query MASK's host state.
 *        Filter fields: asset_id, event_type, start_ts, end_ts. */
char *mask_query_events(struct mask_ipc_context *ctx, const char *filter_json) {
    cJSON *parsed = cJSON_Parse(filter_json);
    if (!parsed) {
        return strdup("{\"error\":\"invalid filter JSON\"}");
    }

    struct query_filter f;
    if (!parse_query_filter(cJSON_GetObjectItemCaseSensitive(parsed, "filter"), &f)) {
        cJSON_Delete(parsed);
        return strdup("{\"error\":\"missing or invalid 'filter' object\"}");
    }
    cJSON_Delete(parsed);

    struct mask_memory_entry entries[256];
    size_t n = mask_ring_buffer_snapshot(ctx->ring, entries, 256);

    cJSON *result = cJSON_CreateObject();
    cJSON *events = cJSON_CreateArray();

    for (size_t i = 0; i < n; i++) {
        /* Parse the event line to check filters */
        cJSON *ev = cJSON_Parse(entries[i].text);
        if (!ev) continue;

        int match = 1;

        /* asset_id filter */
        if (f.asset_id[0]) {
            cJSON *aid = cJSON_GetObjectItemCaseSensitive(ev, "asset_id");
            if (!cJSON_IsString(aid) || strcmp(aid->valuestring, f.asset_id) != 0) {
                match = 0;
            }
        }

        /* event_type filter */
        if (match && f.event_type[0]) {
            cJSON *et = cJSON_GetObjectItemCaseSensitive(ev, "event_type");
            if (!cJSON_IsString(et) || strcmp(et->valuestring, f.event_type) != 0) {
                match = 0;
            }
        }

        /* time range filter */
        if (match) {
            cJSON *ts = cJSON_GetObjectItemCaseSensitive(ev, "timestamp_ms");
            if (cJSON_IsString(ts) && ts->valuestring) {
                uint64_t event_ts = (uint64_t)atoll(ts->valuestring);
                if (event_ts < f.start_ts || event_ts > f.end_ts) {
                    match = 0;
                }
            }
        }

        if (match) {
            cJSON_AddItemToArray(events, ev);  /* cJSON_AddItem transfers ownership */
        } else {
            cJSON_Delete(ev);
        }
    }

    cJSON_AddNumberToObject(result, "count", (double)cJSON_GetArraySize(events));
    cJSON_AddItemToObject(result, "events", events);

    char *out = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    return out;
}
