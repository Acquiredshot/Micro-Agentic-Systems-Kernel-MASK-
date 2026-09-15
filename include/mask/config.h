#ifndef MASK_CONFIG_H
#define MASK_CONFIG_H

#include <stddef.h>

#define MASK_CFG_ENDPOINT_MAX 256
#define MASK_CFG_MODEL_MAX 128

struct mask_config {
    char llm_endpoint[MASK_CFG_ENDPOINT_MAX];
    char llm_model[MASK_CFG_MODEL_MAX];
    int tick_interval_ms;
    int llm_every_n_ticks;
    size_t ring_buffer_capacity;
    int ipc_port;

    /* Live-tunable at runtime via the IPC "set_config" command (see
     * src/ipc.c); not loaded from the environment. Reactor-thread-owned:
     * only main.c's IPC command handling ever writes these, and only the
     * reactor thread reads them, so no locking is needed -- the LLM worker
     * thread gets its own snapshot copy at spawn time instead of reading
     * these fields directly (see llm_worker_main in main.c). */
    int paused;
};

/* Fills cfg with built-in defaults (local Ollama at localhost:11434). */
void mask_config_load_defaults(struct mask_config *cfg);

/* Overrides defaults from environment variables:
 *   MASK_LLM_ENDPOINT, MASK_LLM_MODEL, MASK_TICK_MS, MASK_LLM_EVERY_N_TICKS,
 *   MASK_RING_CAPACITY, MASK_IPC_PORT
 */
void mask_config_load_env(struct mask_config *cfg);

#endif /* MASK_CONFIG_H */
