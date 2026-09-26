#include "mask/ipc.h"
#include "mask/common.h"
#include "mask/event_export.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "cJSON.h"

#define MASK_IPC_MAX_CONNS 16
#define MASK_IPC_REQ_BUF 512
#define MASK_IPC_MAX_ENTRIES 64

#define MASK_CFG_TICK_MS_MIN 100
#define MASK_CFG_TICK_MS_MAX 3600000
#define MASK_CFG_LLM_EVERY_N_MAX 100000
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

struct mask_ipc_conn {
    int fd;
    int in_use;
    char buf[MASK_IPC_REQ_BUF];
    size_t len;
};

/* Fixed arena: bounded number of concurrent client connections, no
 * per-connection heap allocation. A polling GUI client connects, sends one
 * request, reads one response, and disconnects, so this never needs to
 * hold more than a handful of slots at once. */
static struct mask_ipc_conn g_conns[MASK_IPC_MAX_CONNS];
static int g_listen_fd = -1;
static struct mask_ipc_context g_ctx;

static struct mask_ipc_conn *find_free_conn(void) {
    for (int i = 0; i < MASK_IPC_MAX_CONNS; i++) {
        if (!g_conns[i].in_use) {
            return &g_conns[i];
        }
    }
    return NULL;
}

static void close_conn(struct mask_reactor *r, struct mask_ipc_conn *c) {
    mask_reactor_remove_fd(r, c->fd);
    close(c->fd);
    c->in_use = 0;
    c->len = 0;
}

/* Builds a JSON object (not yet stringified) describing the current
 * live-tunable config, for embedding in a snapshot or a get/set_config
 * response. Caller owns the returned object. */
static cJSON *build_config_json(void) {
    cJSON *cfg = cJSON_CreateObject();
    cJSON_AddNumberToObject(cfg, "tick_interval_ms", g_ctx.cfg->tick_interval_ms);
    cJSON_AddNumberToObject(cfg, "llm_every_n_ticks", g_ctx.cfg->llm_every_n_ticks);
    cJSON_AddBoolToObject(cfg, "paused", g_ctx.cfg->paused);
    cJSON_AddStringToObject(cfg, "llm_endpoint", g_ctx.cfg->llm_endpoint);
    cJSON_AddStringToObject(cfg, "llm_model", g_ctx.cfg->llm_model);
    cJSON_AddStringToObject(cfg, "asset_id", g_ctx.cfg->asset_id);
    cJSON_AddStringToObject(cfg, "ioc_data", g_ctx.cfg->ioc_data);
    cJSON_AddStringToObject(cfg, "policy_phase", g_ctx.cfg->policy_phase);
    cJSON_AddStringToObject(cfg, "event_log_path", g_ctx.cfg->event_log_path);
    cJSON_AddStringToObject(cfg, "event_export_url", g_ctx.cfg->event_export_url);
    cJSON_AddStringToObject(cfg, "run_shell_allowlist", g_ctx.cfg->run_shell_allowlist);
    cJSON_AddStringToObject(cfg, "threat_feed_url", g_ctx.cfg->threat_feed_url);
    cJSON_AddNumberToObject(cfg, "threat_feed_interval_ms", g_ctx.cfg->threat_feed_interval_ms);
    return cfg;
}

/* Builds the JSON snapshot returned for the "snapshot" command: recent
 * ring buffer entries, the tool manifest, current config, and basic
 * daemon status. */
static char *build_snapshot_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "tick", (double)*g_ctx.tick_count);
    cJSON_AddBoolToObject(root, "llm_busy", atomic_load(g_ctx.llm_busy));
    cJSON_AddItemToObject(root, "config", build_config_json());

    struct mask_memory_entry recent[MASK_IPC_MAX_ENTRIES];
    size_t n = mask_ring_buffer_snapshot(g_ctx.ring, recent, MASK_IPC_MAX_ENTRIES);

    cJSON *entries = cJSON_CreateArray();
    for (size_t i = 0; i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "ts", (double)recent[i].timestamp_ms);
        cJSON_AddStringToObject(e, "role", recent[i].role);
        cJSON_AddStringToObject(e, "text", recent[i].text);
        cJSON_AddItemToArray(entries, e);
    }
    cJSON_AddItemToObject(root, "entries", entries);

    char *tools_manifest = mask_tool_gateway_manifest_json(g_ctx.tools);
    cJSON *tools_json = tools_manifest ? cJSON_Parse(tools_manifest) : NULL;
    cJSON_AddItemToObject(root, "tools", tools_json ? tools_json : cJSON_CreateArray());
    free(tools_manifest);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_get_config_response(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", 1);
    cJSON_AddItemToObject(root, "config", build_config_json());
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_error_response(const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", 0);
    cJSON_AddStringToObject(root, "error", msg);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* Applies a "set_config" request's partial config object to the live
 * daemon config. Validates every present field before applying any of
 * them, so a bad request never leaves the config half-updated. Returns a
 * freshly allocated JSON response string; caller frees it. */
static char *handle_set_config(cJSON *req_config) {
    if (!cJSON_IsObject(req_config)) {
        return build_error_response("'config' must be an object");
    }

    cJSON *tick_ms = cJSON_GetObjectItemCaseSensitive(req_config, "tick_interval_ms");
    cJSON *every_n = cJSON_GetObjectItemCaseSensitive(req_config, "llm_every_n_ticks");
    cJSON *paused = cJSON_GetObjectItemCaseSensitive(req_config, "paused");
    cJSON *endpoint = cJSON_GetObjectItemCaseSensitive(req_config, "llm_endpoint");
    cJSON *model = cJSON_GetObjectItemCaseSensitive(req_config, "llm_model");
    cJSON *asset_id = cJSON_GetObjectItemCaseSensitive(req_config, "asset_id");
    cJSON *ioc_data = cJSON_GetObjectItemCaseSensitive(req_config, "ioc_data");
    cJSON *policy_phase = cJSON_GetObjectItemCaseSensitive(req_config, "policy_phase");
    cJSON *event_log_path = cJSON_GetObjectItemCaseSensitive(req_config, "event_log_path");
    cJSON *run_shell_allowlist = cJSON_GetObjectItemCaseSensitive(req_config, "run_shell_allowlist");
    cJSON *threat_feed_url = cJSON_GetObjectItemCaseSensitive(req_config, "threat_feed_url");
    cJSON *threat_feed_interval = cJSON_GetObjectItemCaseSensitive(req_config, "threat_feed_interval_ms");

    if (tick_ms && (!cJSON_IsNumber(tick_ms) ||
                    tick_ms->valuedouble < MASK_CFG_TICK_MS_MIN ||
                    tick_ms->valuedouble > MASK_CFG_TICK_MS_MAX)) {
        return build_error_response("tick_interval_ms must be a number in [100, 3600000]");
    }
    if (every_n && (!cJSON_IsNumber(every_n) ||
                    every_n->valuedouble < 0 ||
                    every_n->valuedouble > MASK_CFG_LLM_EVERY_N_MAX)) {
        return build_error_response("llm_every_n_ticks must be a non-negative number");
    }
    if (paused && !cJSON_IsBool(paused)) {
        return build_error_response("paused must be a boolean");
    }
    if (endpoint && (!cJSON_IsString(endpoint) || !endpoint->valuestring[0] ||
                     strlen(endpoint->valuestring) >= MASK_CFG_ENDPOINT_MAX)) {
        return build_error_response("llm_endpoint must be a non-empty string");
    }
    if (model && (!cJSON_IsString(model) || !model->valuestring[0] ||
                  strlen(model->valuestring) >= MASK_CFG_MODEL_MAX)) {
        return build_error_response("llm_model must be a non-empty string");
    }
    if (asset_id && (!cJSON_IsString(asset_id) ||
                     strlen(asset_id->valuestring) >= MASK_CFG_ASSET_ID_MAX)) {
        return build_error_response("asset_id must be a string shorter than 64");
    }
    if (ioc_data && (!cJSON_IsString(ioc_data) ||
                     strlen(ioc_data->valuestring) >= MASK_CFG_IOC_MAX)) {
        return build_error_response("ioc_data must be a string shorter than 4096");
    }
    if (policy_phase && (!cJSON_IsString(policy_phase) ||
                        strlen(policy_phase->valuestring) >= MASK_CFG_PHASE_MAX)) {
        return build_error_response("policy_phase must be a string shorter than 32");
    }
    if (event_log_path && (!cJSON_IsString(event_log_path) ||
                           strlen(event_log_path->valuestring) >= MASK_CFG_PATH_MAX)) {
        return build_error_response("event_log_path must be a string shorter than " STRINGIFY(MASK_CFG_PATH_MAX));
    }

    /* All present fields validated -- now apply. */
    if (tick_ms) {
        int new_interval = (int)tick_ms->valuedouble;
        if (new_interval != g_ctx.cfg->tick_interval_ms) {
            g_ctx.cfg->tick_interval_ms = new_interval;
            mask_reactor_rearm_timer(g_ctx.tick_timer_fd, new_interval);
        }
    }
    if (every_n) {
        g_ctx.cfg->llm_every_n_ticks = (int)every_n->valuedouble;
    }
    if (paused) {
        g_ctx.cfg->paused = cJSON_IsTrue(paused) ? 1 : 0;
    }
    if (endpoint) {
        snprintf(g_ctx.cfg->llm_endpoint, sizeof(g_ctx.cfg->llm_endpoint), "%s", endpoint->valuestring);
    }
    if (model) {
        snprintf(g_ctx.cfg->llm_model, sizeof(g_ctx.cfg->llm_model), "%s", model->valuestring);
    }
    if (asset_id) {
        snprintf(g_ctx.cfg->asset_id, sizeof(g_ctx.cfg->asset_id), "%s", asset_id->valuestring);
    }
    if (ioc_data) {
        snprintf(g_ctx.cfg->ioc_data, sizeof(g_ctx.cfg->ioc_data), "%s", ioc_data->valuestring);
    }
    if (policy_phase) {
        snprintf(g_ctx.cfg->policy_phase, sizeof(g_ctx.cfg->policy_phase), "%s", policy_phase->valuestring);
    }
    if (run_shell_allowlist) {
        snprintf(g_ctx.cfg->run_shell_allowlist, sizeof(g_ctx.cfg->run_shell_allowlist), "%s", run_shell_allowlist->valuestring);
    }
    if (threat_feed_url) {
        snprintf(g_ctx.cfg->threat_feed_url, sizeof(g_ctx.cfg->threat_feed_url), "%s", threat_feed_url->valuestring);
    }
    if (threat_feed_interval) {
        g_ctx.cfg->threat_feed_interval_ms = (int)threat_feed_interval->valuedouble;
    }
    if (event_log_path) {
        snprintf(g_ctx.cfg->event_log_path, sizeof(g_ctx.cfg->event_log_path), "%s", event_log_path->valuestring);
        /* Reopen the event log under the new path (closes old fd if open). */
        mask_event_log_close(&g_ctx.cfg->event_log_fd);
        g_ctx.cfg->event_log_fd = mask_event_log_open(g_ctx.cfg);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", 1);
    cJSON_AddItemToObject(root, "config", build_config_json());
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* Wire protocol: one line of JSON per request, e.g. {"cmd":"snapshot"},
 * {"cmd":"get_config"}, or {"cmd":"set_config","config":{...partial...}}. */
static void handle_request(struct mask_reactor *r, struct mask_ipc_conn *c, const char *req) {
    char *response = NULL;

    cJSON *parsed = cJSON_Parse(req);
    cJSON *cmd = parsed ? cJSON_GetObjectItemCaseSensitive(parsed, "cmd") : NULL;
    const char *cmd_str = cJSON_IsString(cmd) ? cmd->valuestring : NULL;

    if (cmd_str && strcmp(cmd_str, "snapshot") == 0) {
        response = build_snapshot_json();
    } else if (cmd_str && strcmp(cmd_str, "get_config") == 0) {
        response = build_get_config_response();
    } else if (cmd_str && strcmp(cmd_str, "set_config") == 0) {
        response = handle_set_config(cJSON_GetObjectItemCaseSensitive(parsed, "config"));
    } else if (cmd_str && strcmp(cmd_str, "query_events") == 0) {
        cJSON *filter = cJSON_GetObjectItemCaseSensitive(parsed, "filter");
        /* Build a wrapper: mask_query_events expects {"filter": {...}} */
        cJSON *wrapper = cJSON_CreateObject();
        if (filter && cJSON_IsObject(filter)) {
            /* Clone the filter so we don't move it between cJSON trees */
            cJSON *clone = cJSON_Duplicate(filter, 1);
            cJSON_AddItemToObject(wrapper, "filter", clone);
        } else {
            cJSON_AddItemToObject(wrapper, "filter", cJSON_CreateObject());
        }
        const char *filter_str = cJSON_PrintUnformatted(wrapper);
        response = mask_query_events(&g_ctx, filter_str);
        free((char *)filter_str);
        cJSON_Delete(wrapper);
    } else {
        response = build_error_response("unknown or missing 'cmd'");
    }
    if (parsed) {
        cJSON_Delete(parsed);
    }

    if (response) {
        /* Single write (JSON + newline together) rather than two separate
         * writes: over a real TCP socket (unlike the Unix socket this
         * started as), splitting a tiny reply across writes can trip
         * Nagle's algorithm and add latency for no reason. */
        size_t rlen = strlen(response);
        response[rlen] = '\n'; /* overwrite the NUL terminator; write() below uses `total`, not strlen() */
        size_t total = rlen + 1;

        /* Best-effort write: a polling status client that's already gone
         * (e.g. GUI closed mid-request) is not worth retrying for. */
        ssize_t written = write(c->fd, response, total);
        if (written >= 0 && (size_t)written < total) {
            MASK_LOGW("ipc: short write to client (%zd/%zu bytes)", written, total);
        }
        free(response);
    }

    close_conn(r, c);
}

static void on_conn_readable(struct mask_reactor *r, int fd, uint32_t events, void *user_data) {
    (void)events;
    struct mask_ipc_conn *c = (struct mask_ipc_conn *)user_data;

    ssize_t n = read(fd, c->buf + c->len, sizeof(c->buf) - 1 - c->len);
    if (n <= 0) {
        close_conn(r, c);
        return;
    }
    c->len += (size_t)n;
    c->buf[c->len] = '\0';

    if (memchr(c->buf, '\n', c->len) != NULL || c->len >= sizeof(c->buf) - 1) {
        handle_request(r, c, c->buf);
    }
}

static void on_listen_readable(struct mask_reactor *r, int fd, uint32_t events, void *user_data) {
    (void)events;
    (void)user_data;

    for (;;) {
        int client_fd = accept(fd, NULL, NULL);
        if (client_fd < 0) {
            break; /* EAGAIN: no more pending connections right now */
        }

        struct mask_ipc_conn *c = find_free_conn();
        if (!c) {
            MASK_LOGW("ipc: connection pool full, dropping client");
            close(client_fd);
            continue;
        }

        c->fd = client_fd;
        c->in_use = 1;
        c->len = 0;

        if (mask_reactor_add_fd(r, client_fd, EPOLLIN, on_conn_readable, c) != MASK_OK) {
            c->in_use = 0;
            close(client_fd);
        }
    }
}

int mask_ipc_init(struct mask_reactor *r, int port, const struct mask_ipc_context *ctx) {
    memset(g_conns, 0, sizeof(g_conns));
    g_ctx = *ctx;

    g_listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (g_listen_fd < 0) {
        MASK_LOGE("ipc: socket() failed: %s", strerror(errno));
        return MASK_ERR;
    }

    int reuse = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 only, never external */

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        MASK_LOGE("ipc: bind(127.0.0.1:%d) failed: %s", port, strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return MASK_ERR;
    }

    if (listen(g_listen_fd, 8) != 0) {
        MASK_LOGE("ipc: listen() failed: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return MASK_ERR;
    }

    if (mask_reactor_add_fd(r, g_listen_fd, EPOLLIN, on_listen_readable, NULL) != MASK_OK) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return MASK_ERR;
    }

    MASK_LOGI("ipc: listening on 127.0.0.1:%d", port);
    return MASK_OK;
}

void mask_ipc_shutdown(void) {
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}
