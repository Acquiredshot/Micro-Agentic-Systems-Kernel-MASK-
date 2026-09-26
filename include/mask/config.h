#ifndef MASK_CONFIG_H
#define MASK_CONFIG_H

#include <stddef.h>

#define MASK_CFG_ENDPOINT_MAX 256
#define MASK_CFG_MODEL_MAX 128
#define MASK_CFG_ASSET_ID_MAX 64
#define MASK_CFG_IOC_MAX 4096
#define MASK_CFG_PHASE_MAX 32
#define MASK_CFG_PATH_MAX 512
#define MASK_CFG_URL_MAX 256
#define MASK_CFG_ALLOWLIST_MAX 1024

struct mask_config {
    char llm_endpoint[MASK_CFG_ENDPOINT_MAX];
    char llm_model[MASK_CFG_MODEL_MAX];
    int tick_interval_ms;
    int llm_every_n_ticks;
    size_t ring_buffer_capacity;
    int ipc_port;
    char asset_id[MASK_CFG_ASSET_ID_MAX];
    char ioc_data[MASK_CFG_IOC_MAX];
    char policy_phase[MASK_CFG_PHASE_MAX];
    char event_log_path[MASK_CFG_PATH_MAX];
    char event_export_url[MASK_CFG_URL_MAX];
    char run_shell_allowlist[MASK_CFG_ALLOWLIST_MAX];
    char threat_feed_url[MASK_CFG_URL_MAX];
    int threat_feed_interval_ms;

    /* Live-tunable at runtime via the IPC "set_config" command (see
     * src/ipc.c); not loaded from the environment. Reactor-thread-owned:
     * only main.c's IPC command handling ever writes these, and only the
     * reactor thread reads them, so no locking is needed. */
    int paused;
    int event_log_fd;
    int event_export_disable;
};

/* The three policy phases. In "observe" mode only read-only tools are
 * available; in "investigate" run_shell is added for ad-hoc digging;
 * in "respond" the structured action tool is available for executing
 * response playbooks. */
#define MASK_POLICY_PHASE_OBSERVE   "observe"
#define MASK_POLICY_PHASE_INVESTIGATE "investigate"
#define MASK_POLICY_PHASE_RESPOND   "respond"

/* Fills cfg with built-in defaults (local Ollama at localhost:11434). */
void mask_config_load_defaults(struct mask_config *cfg);

/* Overrides defaults from environment variables:
 *   MASK_LLM_ENDPOINT, MASK_LLM_MODEL, MASK_TICK_MS, MASK_LLM_EVERY_N_TICKS,
 *   MASK_RING_CAPACITY, MASK_IPC_PORT, MASK_ASSET_ID,
 *   MASK_IOC_DATA, MASK_POLICY_PHASE, MASK_EVENT_LOG_PATH
 */
void mask_config_load_env(struct mask_config *cfg);

#endif /* MASK_CONFIG_H */
