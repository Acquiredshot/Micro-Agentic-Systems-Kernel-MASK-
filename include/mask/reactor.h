#ifndef MASK_REACTOR_H
#define MASK_REACTOR_H

#include <stdint.h>
#include <signal.h>

#define MASK_REACTOR_MAX_HANDLERS 256

struct mask_reactor;

typedef void (*mask_fd_handler)(struct mask_reactor *r, int fd, uint32_t events, void *user_data);

struct mask_fd_entry {
    int fd;
    int in_use;
    mask_fd_handler handler;
    void *user_data;
};

struct mask_reactor {
    int epfd;
    struct mask_fd_entry handlers[MASK_REACTOR_MAX_HANDLERS];
    volatile sig_atomic_t running;
};

int mask_reactor_init(struct mask_reactor *r);
void mask_reactor_destroy(struct mask_reactor *r);

/* Registers fd for the given epoll event mask (EPOLLIN, etc). The handler
 * is invoked from mask_reactor_run() whenever fd becomes ready. */
int mask_reactor_add_fd(struct mask_reactor *r, int fd, uint32_t events,
                         mask_fd_handler handler, void *user_data);
int mask_reactor_remove_fd(struct mask_reactor *r, int fd);

/* Blocks, dispatching events until mask_reactor_stop() is called. */
int mask_reactor_run(struct mask_reactor *r);
void mask_reactor_stop(struct mask_reactor *r);

/* Convenience: creates a signalfd for the given signals, blocks them from
 * default delivery, and registers it on the reactor. SIGINT/SIGTERM
 * delivered this way call mask_reactor_stop() automatically; other signals
 * are passed to the optional extra_handler. */
int mask_reactor_watch_signals(struct mask_reactor *r, const int *signals, int nsignals,
                                mask_fd_handler extra_handler, void *user_data);

/* Convenience: creates a timerfd firing every interval_ms and registers it
 * on the reactor with the given handler. If out_fd is non-NULL, the
 * timer's fd is written there so the caller can later re-arm its period
 * with mask_reactor_rearm_timer() -- e.g. in response to a live config
 * change. */
int mask_reactor_add_timer(struct mask_reactor *r, int interval_ms,
                            mask_fd_handler handler, void *user_data, int *out_fd);

/* Changes the firing period of a timerfd previously created by
 * mask_reactor_add_timer(), without touching its registration on the
 * reactor. Returns MASK_OK/MASK_ERR. */
int mask_reactor_rearm_timer(int timer_fd, int interval_ms);

#endif /* MASK_REACTOR_H */
