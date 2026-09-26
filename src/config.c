#include "mask/config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void mask_config_load_defaults(struct mask_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->llm_endpoint, sizeof(cfg->llm_endpoint), "http://127.0.0.1:11434");
    snprintf(cfg->llm_model, sizeof(cfg->llm_model), "llama3.2");
    cfg->tick_interval_ms = 5000;
    cfg->llm_every_n_ticks = 6; /* reason about state roughly every 30s by default */
    cfg->ring_buffer_capacity = 256;
    cfg->ipc_port = 7717;
}

static void override_str(char *dst, size_t dst_size, const char *env_name) {
    const char *v = getenv(env_name);
    if (v && *v) {
        snprintf(dst, dst_size, "%s", v);
    }
}

static void override_int(int *dst, const char *env_name) {
    const char *v = getenv(env_name);
    if (v && *v) {
        *dst = atoi(v);
    }
}

static void override_size(size_t *dst, const char *env_name) {
    const char *v = getenv(env_name);
    if (v && *v) {
        long parsed = atol(v);
        if (parsed > 0) {
            *dst = (size_t)parsed;
        }
    }
}

void mask_config_load_env(struct mask_config *cfg) {
    override_str(cfg->llm_endpoint, sizeof(cfg->llm_endpoint), "MASK_LLM_ENDPOINT");
    override_str(cfg->llm_model, sizeof(cfg->llm_model), "MASK_LLM_MODEL");
    override_int(&cfg->tick_interval_ms, "MASK_TICK_MS");
    override_int(&cfg->llm_every_n_ticks, "MASK_LLM_EVERY_N_TICKS");
    override_size(&cfg->ring_buffer_capacity, "MASK_RING_CAPACITY");
    override_int(&cfg->ipc_port, "MASK_IPC_PORT");
    override_str(cfg->asset_id, sizeof(cfg->asset_id), "MASK_ASSET_ID");
    override_str(cfg->ioc_data, sizeof(cfg->ioc_data), "MASK_IOC_DATA");
    override_str(cfg->policy_phase, sizeof(cfg->policy_phase), "MASK_POLICY_PHASE");
    override_str(cfg->event_log_path, sizeof(cfg->event_log_path), "MASK_EVENT_LOG_PATH");
}
