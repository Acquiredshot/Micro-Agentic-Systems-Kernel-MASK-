#include "mask/config.h"
#include "mask/common.h"
#include "mask/ring_buffer.h"
#include "mask/reactor.h"
#include "mask/tool_gateway.h"
#include "mask/llm_client.h"
#include "mask/ipc.h"
#include "mask/event_export.h"
#include "mask/sandbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include "cJSON.h"

int tool_sysinfo(const char *args_json, char *output, size_t output_size);
int tool_run_shell(const char *args_json, char *output, size_t output_size);
void mask_shell_set_allowlist(const char *allowlist);
int tool_net_connections(const char *args_json, char *output, size_t output_size);
int tool_listening_sockets(const char *args_json, char *output, size_t output_size);
int tool_process_list(const char *args_json, char *output, size_t output_size);
int tool_user_sessions(const char *args_json, char *output, size_t output_size);
int tool_action(const char *args_json, char *output, size_t output_size);

struct mask_daemon {
    struct mask_config cfg;
    struct mask_ring_buffer ring;
    struct mask_tool_gateway tools;
    struct mask_reactor reactor;
    atomic_int llm_busy;
    unsigned long tick_count;
    int tick_timer_fd;
    struct mask_ipc_context ipc_ctx;
};

/* Config values an in-flight LLM worker needs are copied into this struct
 * on the reactor thread, at the moment the worker is spawned, rather than
 * read from struct mask_daemon's cfg directly inside the worker thread.
 * cfg.llm_endpoint/llm_model can now change mid-cycle via the IPC
 * "set_config" command (see ipc.c), and that command handler also runs on
 * the reactor thread -- reading a private snapshot here is what makes the
 * two threads race-free without needing a lock on the strings themselves. */
struct mask_llm_job {
    struct mask_daemon *d;
    char llm_endpoint[MASK_CFG_ENDPOINT_MAX];
    char llm_model[MASK_CFG_MODEL_MAX];
    char asset_id[MASK_CFG_ASSET_ID_MAX];
    char ioc_data[MASK_CFG_IOC_MAX];
    int policy_phase;
};

/* Runs on a detached worker thread so the reactor's epoll loop is never
 * blocked on network I/O to the LLM backend. Only one of these runs at a
 * time (gated by llm_busy), so it is free to use static/shared scratch
 * buffers without its own locking. */
static void *llm_worker_main(void *arg) {
    struct mask_llm_job *job = (struct mask_llm_job *)arg;
    struct mask_daemon *d = job->d;

    struct mask_memory_entry recent[16];
    size_t n = mask_ring_buffer_snapshot(&d->ring, recent, 16);

    char *manifest = mask_tool_gateway_manifest_json(&d->tools);

    char prompt[4096];
    int off = snprintf(prompt, sizeof(prompt),
        "You are MASK, the Network/Asset Intelligence agent of the WOLF-PAK "
        "security platform. Your host is asset '%s'. Current policy phase: %s.\n"
        "Threat intelligence indicators (IOCs) — treat any observation matching "
        "these as high severity: %s\n"
        "Available tools (call at most one per cycle, by replying with JSON "
        "of the form {\"tool\":\"<name>\",\"args\":{...}} or "
        "{\"tool\":null,\"say\":\"<note>\"} if no action is needed): %s\n"
        "\nRecent observations:\n",
        job->asset_id[0] ? job->asset_id : "unknown",
        job->policy_phase ? "respond" : (job->policy_phase == 1 ? "investigate" : "observe"),
        job->ioc_data[0] ? job->ioc_data : "(none loaded)",
        manifest ? manifest : "[]");
    free(manifest);

    for (size_t i = 0; i < n && off < (int)sizeof(prompt) - 128; i++) {
        off += snprintf(prompt + off, sizeof(prompt) - off, "[%s] %s\n",
                         recent[i].role, recent[i].text);
    }

    char response[2048];
    if (mask_llm_chat(job->llm_endpoint, job->llm_model,
                       NULL, prompt, response, sizeof(response)) != MASK_OK) {
        MASK_LOGW("LLM call failed this cycle, skipping");
        mask_ring_buffer_push(&d->ring, "llm_error", "request failed or endpoint unreachable");
        atomic_store(&d->llm_busy, 0);
        free(job);
        return NULL;
    }

    mask_ring_buffer_push(&d->ring, "llm", response);
    MASK_LOGI("LLM reply: %.200s", response);

    cJSON *decision = cJSON_Parse(response);
    if (decision) {
        cJSON *tool_name = cJSON_GetObjectItemCaseSensitive(decision, "tool");
        if (cJSON_IsString(tool_name) && tool_name->valuestring && *tool_name->valuestring) {
            cJSON *tool_args = cJSON_GetObjectItemCaseSensitive(decision, "args");
            char *args_str = tool_args ? cJSON_PrintUnformatted(tool_args) : strdup("{}");

            char tool_output[1024];
            int rc = mask_tool_gateway_dispatch(&d->tools, tool_name->valuestring,
                                                 args_str, tool_output, sizeof(tool_output),
                                                 job->policy_phase);

            /* Update the run_shell allowlist pointer so the tool sees the
             * latest config (set via IPC set_config) without a restart. */
            mask_shell_set_allowlist(d->cfg.run_shell_allowlist);

            /* Wrap the tool's raw output in a standard event envelope so the
             * ring buffer and any downstream consumers (Event Fabric, Security
             * Graph, cross-app correlators) get structured events. */
            uint64_t now = mask_now_ms();
            char *envelope = mask_event_envelope(now, job->asset_id,
                tool_name->valuestring, "info", tool_output);
            char logline[2048];
            if (envelope) {
                snprintf(logline, sizeof(logline), "%s", envelope);
                free(envelope);
            } else {
                snprintf(logline, sizeof(logline),
                         "{\"timestamp_ms\":%" PRIu64 ",\"asset_id\":\"%s\","
                         "\"source\":\"MASK\",\"event_type\":\"%s\",\"severity\":\"info\","
                         "\"payload\":%s}",
                         now, job->asset_id, tool_name->valuestring, tool_output);
            }

            mask_ring_buffer_push(&d->ring, "tool", logline);
            if (d->cfg.event_log_fd >= 0) {
                mask_event_log_write(d->cfg.event_log_fd, logline);
            }
            MASK_LOGI("dispatched tool '%s' -> rc=%d out=%.200s",
                      tool_name->valuestring, rc, tool_output);

            free(args_str);
        } else {
            MASK_LOGI("LLM chose no tool this cycle");
        }
        cJSON_Delete(decision);
    } else {
        MASK_LOGD("LLM response was not a tool-call JSON, treating as commentary");
    }

    atomic_store(&d->llm_busy, 0);
    free(job);
    return NULL;
}

static void on_timer_tick(struct mask_reactor *r, int fd, uint32_t events, void *user_data) {
    (void)r;
    (void)events;
    struct mask_daemon *d = (struct mask_daemon *)user_data;

    int log_fd = d->cfg.event_log_fd;

    uint64_t expirations;
    if (read(fd, &expirations, sizeof(expirations)) != sizeof(expirations)) {
        return;
    }

    d->tick_count++;

    /* Periodically poll the threat intel feed (if configured). */
    if (d->cfg.threat_feed_url[0] && d->cfg.threat_feed_interval_ms > 0 &&
        d->tick_count % (unsigned long)(d->cfg.threat_feed_interval_ms / d->cfg.tick_interval_ms) == 0) {
        mask_threat_feed_trigger(&d->ipc_ctx);
    }

    char sysinfo_out[256];
    if (tool_sysinfo(NULL, sysinfo_out, sizeof(sysinfo_out)) == MASK_OK) {
        uint64_t now = mask_now_ms();
        char *envelope = mask_event_envelope(now, d->cfg.asset_id,
            "sysinfo", "info", sysinfo_out);
        if (envelope) {
            mask_ring_buffer_push(&d->ring, "sysinfo", envelope);
            free(envelope);
        } else {
            mask_ring_buffer_push(&d->ring, "sysinfo", sysinfo_out);
        }

        if (log_fd >= 0) {
            char *jline = mask_event_envelope(now, d->cfg.asset_id,
                "sysinfo", "info", sysinfo_out);
            if (jline) {
                mask_event_log_write(log_fd, jline);
                free(jline);
            }
        }
    }

    if (!d->cfg.paused && d->cfg.llm_every_n_ticks > 0 &&
        d->tick_count % (unsigned long)d->cfg.llm_every_n_ticks == 0) {
        int expected = 0;
        if (atomic_compare_exchange_strong(&d->llm_busy, &expected, 1)) {
            struct mask_llm_job *job = malloc(sizeof(*job));
            if (!job) {
                MASK_LOGE("out of memory allocating LLM job");
                atomic_store(&d->llm_busy, 0);
                return;
            }
            job->d = d;
            snprintf(job->llm_endpoint, sizeof(job->llm_endpoint), "%s", d->cfg.llm_endpoint);
            snprintf(job->llm_model, sizeof(job->llm_model), "%s", d->cfg.llm_model);
            snprintf(job->asset_id, sizeof(job->asset_id), "%s", d->cfg.asset_id);
            snprintf(job->ioc_data, sizeof(job->ioc_data), "%s", d->cfg.ioc_data);
            job->policy_phase = (d->cfg.policy_phase[0] == 'r') ? MASK_TOOL_PHASE_RESPOND :
                                (d->cfg.policy_phase[0] == 'i') ? MASK_TOOL_PHASE_INVESTIGATE :
                                MASK_TOOL_PHASE_OBSERVE;

            pthread_t worker;
            if (pthread_create(&worker, NULL, llm_worker_main, job) == 0) {
                pthread_detach(worker);
            } else {
                MASK_LOGE("failed to spawn LLM worker thread");
                free(job);
                atomic_store(&d->llm_busy, 0);
            }
        } else {
            MASK_LOGD("skipping LLM cycle, previous one still in flight");
        }
    }
}

int main(void) {
    struct mask_daemon daemon;
    memset(&daemon, 0, sizeof(daemon));
    atomic_init(&daemon.llm_busy, 0);

    mask_config_load_defaults(&daemon.cfg);
    mask_config_load_env(&daemon.cfg);

    MASK_LOGI("MASK starting: endpoint=%s model=%s tick=%dms llm_every=%d ticks",
              daemon.cfg.llm_endpoint, daemon.cfg.llm_model,
              daemon.cfg.tick_interval_ms, daemon.cfg.llm_every_n_ticks);

    if (mask_ring_buffer_init(&daemon.ring, daemon.cfg.ring_buffer_capacity) != MASK_OK) {
        MASK_LOGE("failed to init ring buffer");
        return EXIT_FAILURE;
    }

    if (mask_llm_client_global_init() != MASK_OK) {
        MASK_LOGE("failed to init LLM client");
        mask_ring_buffer_destroy(&daemon.ring);
        return EXIT_FAILURE;
    }

    mask_tool_gateway_init(&daemon.tools);
    mask_tool_gateway_register(&daemon.tools, "sysinfo",
        "Returns current load average and memory info as JSON.", tool_sysinfo,
        MASK_TOOL_PHASE_OBSERVE);
    mask_tool_gateway_register(&daemon.tools, "run_shell",
        "Runs a sandboxed command. Args: {\"argv\":[\"cmd\",\"arg1\",...]}. "
        "Requires investigate phase.", tool_run_shell,
        MASK_TOOL_PHASE_INVESTIGATE);
    mask_tool_gateway_register(&daemon.tools, "net_connections",
        "Returns active TCP/UDP connections. Optional args: {\"proto\":\"tcp\"|\"udp\"}. "
        "Output: array of {proto,state,local,peer,pid,process}.", tool_net_connections,
        MASK_TOOL_PHASE_OBSERVE);
    mask_tool_gateway_register(&daemon.tools, "listening_sockets",
        "Returns TCP/UDP sockets in LISTEN state. Args: {}. "
        "Output: array of {proto,local,peer,pid,process}.", tool_listening_sockets,
        MASK_TOOL_PHASE_OBSERVE);
    mask_tool_gateway_register(&daemon.tools, "process_list",
        "Returns all running processes. Optional args: {\"extra\":\"cmdline\"}. "
        "Output: array of {pid,user,stat,cpu,mem,vsz,rss,comm[,cmdline]}.", tool_process_list,
        MASK_TOOL_PHASE_OBSERVE);
    mask_tool_gateway_register(&daemon.tools, "user_sessions",
        "Returns currently logged-in users. Args: {}. "
        "Output: array of {user,tty,login[,host]}.", tool_user_sessions,
        MASK_TOOL_PHASE_OBSERVE);
    mask_tool_gateway_register(&daemon.tools, "action",
        "Structured response actions: kill_process, disable_user, isolate_network, "
        "rotate_credential. Requires respond phase.", tool_action,
        MASK_TOOL_PHASE_RESPOND);

    if (mask_reactor_init(&daemon.reactor) != MASK_OK) {
        MASK_LOGE("failed to init reactor");
        goto shutdown;
    }

    {
        int sigs[] = { SIGINT, SIGTERM };
        if (mask_reactor_watch_signals(&daemon.reactor, sigs, 2, NULL, NULL) != MASK_OK) {
            MASK_LOGE("failed to install signal handling");
            goto shutdown;
        }
    }

    if (mask_reactor_add_timer(&daemon.reactor, daemon.cfg.tick_interval_ms,
                                on_timer_tick, &daemon, &daemon.tick_timer_fd) != MASK_OK) {
        MASK_LOGE("failed to install tick timer");
        goto shutdown;
    }

    {
        struct mask_ipc_context ipc_ctx = {
            .ring = &daemon.ring,
            .tools = &daemon.tools,
            .tick_count = &daemon.tick_count,
            .llm_busy = &daemon.llm_busy,
            .cfg = &daemon.cfg,
            .tick_timer_fd = daemon.tick_timer_fd,
        };
        if (mask_ipc_init(&daemon.reactor, daemon.cfg.ipc_port, &ipc_ctx) != MASK_OK) {
            MASK_LOGE("failed to init IPC server");
            goto shutdown;
        }
    }

    mask_ring_buffer_push(&daemon.ring, "system", "MASK daemon started");

    /* Open the per-host event log file (if a path is configured). */
    daemon.cfg.event_log_fd = mask_event_log_open(&daemon.cfg);

    /* Populate the IPC context that the timer tick and export thread need. */
    daemon.ipc_ctx = (struct mask_ipc_context){
        .ring = &daemon.ring,
        .tools = &daemon.tools,
        .tick_count = &daemon.tick_count,
        .llm_busy = &daemon.llm_busy,
        .cfg = &daemon.cfg,
        .tick_timer_fd = daemon.tick_timer_fd,
    };

    /* Start the outbound event export thread (if a URL is configured). */
    mask_event_export_start(&daemon.ipc_ctx);

    mask_reactor_run(&daemon.reactor);

    MASK_LOGI("waiting for in-flight LLM worker (if any) to finish");
    for (int waited_ms = 0; atomic_load(&daemon.llm_busy) && waited_ms < 65000; waited_ms += 100) {
        usleep(100 * 1000);
    }
    mask_ring_buffer_push(&daemon.ring, "system", "MASK daemon stopping");

shutdown:
    mask_event_log_close(&daemon.cfg.event_log_fd);
    mask_ipc_shutdown();
    mask_reactor_destroy(&daemon.reactor);
    mask_llm_client_global_cleanup();
    mask_ring_buffer_destroy(&daemon.ring);

    MASK_LOGI("MASK stopped cleanly");
    return EXIT_SUCCESS;
}
