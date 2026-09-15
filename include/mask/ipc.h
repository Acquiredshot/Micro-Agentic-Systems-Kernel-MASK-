#ifndef MASK_IPC_H
#define MASK_IPC_H

#include "mask/reactor.h"
#include "mask/ring_buffer.h"
#include "mask/tool_gateway.h"
#include "mask/config.h"

#include <stdatomic.h>

/* Snapshot of daemon state the IPC server reports to clients, plus the
 * handles it needs to apply a live "set_config" command. Populated by the
 * caller (main.c) before mask_ipc_init(). tick_count is only ever touched
 * by the reactor thread (same thread the IPC handlers run on), so it needs
 * no atomics; llm_busy is shared with the LLM worker thread and is already
 * atomic_int in main.c. cfg is mutated in place by "set_config" -- safe
 * because only the reactor thread ever writes to it (see config.h). */
struct mask_ipc_context {
    struct mask_ring_buffer *ring;
    struct mask_tool_gateway *tools;
    const unsigned long *tick_count;
    const atomic_int *llm_busy;
    struct mask_config *cfg;
    int tick_timer_fd; /* re-armed via mask_reactor_rearm_timer() on tick_interval_ms change */
};

/* Binds a TCP socket on 127.0.0.1:port, listens, and registers it on the
 * reactor. Loopback-only: this is meant for local clients (a GUI, a CLI
 * inspector), including one running natively on Windows against a daemon
 * in WSL2, which auto-forwards loopback TCP ports but not Unix sockets.
 * Returns MASK_OK/MASK_ERR. */
int mask_ipc_init(struct mask_reactor *r, int port,
                   const struct mask_ipc_context *ctx);

/* Closes the listening socket. Safe to call even if mask_ipc_init() was
 * never called or failed. */
void mask_ipc_shutdown(void);

#endif /* MASK_IPC_H */
