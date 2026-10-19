#ifndef MASK_REACTOR_H
#define MASK_REACTOR_H

#include <stdint.h>
#include <signal.h>

/**
 * @file reactor.h
 * @brief Fixed-capacity epoll reactor used to drive MASK's event loop.
 */

#define MASK_REACTOR_MAX_HANDLERS 256

struct mask_reactor;

/**
 * @brief Callback invoked when a registered file descriptor becomes ready.
 *
 * @param r Reactor instance that owns the fd.
 * @param fd Ready file descriptor.
 * @param events epoll event bitmask.
 * @param user_data Caller-provided context passed at registration time.
 */
typedef void (*mask_fd_handler)(struct mask_reactor *r, int fd, uint32_t events, void *user_data);

/**
 * @brief Registered entry in the reactor's fixed-size file-descriptor table.
 */
struct mask_fd_entry {
    int fd;
    int in_use;
    mask_fd_handler handler;
    void *user_data;
};

/**
 * @brief Main reactor context.
 */
struct mask_reactor {
    int epfd;
    struct mask_fd_entry handlers[MASK_REACTOR_MAX_HANDLERS];
    volatile sig_atomic_t running;
};

/**
 * @brief Initializes the reactor and creates the epoll instance.
 *
 * @param r Reactor to initialize.
 * @return MASK_OK on success, MASK_ERR otherwise.
 */
int mask_reactor_init(struct mask_reactor *r);

/**
 * @brief Releases all resources owned by the reactor.
 * @param r Reactor to destroy.
 */
void mask_reactor_destroy(struct mask_reactor *r);

/**
 * @brief Registers an fd against the reactor and its epoll event mask.
 *
 * @param r Reactor instance.
 * @param fd File descriptor to monitor.
 * @param events epoll event flags for this fd.
 * @param handler Callback to invoke when the fd becomes ready.
 * @param user_data Context for the callback.
 * @return MASK_OK on success, MASK_ERR on registration failure.
 */
int mask_reactor_add_fd(struct mask_reactor *r, int fd, uint32_t events,
                         mask_fd_handler handler, void *user_data);

/**
 * @brief Removes a previously registered fd from the reactor.
 * @param r Reactor instance.
 * @param fd File descriptor to unregister.
 * @return MASK_OK on success, MASK_ERR if not found.
 */
int mask_reactor_remove_fd(struct mask_reactor *r, int fd);

/**
 * @brief Runs the reactor until mask_reactor_stop() is called.
 * @param r Reactor instance.
 * @return MASK_OK on normal shutdown, MASK_ERR on epoll failure.
 */
int mask_reactor_run(struct mask_reactor *r);

/**
 * @brief Signals the reactor to stop processing events.
 * @param r Reactor instance.
 */
void mask_reactor_stop(struct mask_reactor *r);

/**
 * @brief Watches one or more signals via signalfd and dispatches them.
 *
 * @param r Reactor instance.
 * @param signals Array of signal numbers to monitor.
 * @param nsignals Number of signals in the array.
 * @param extra_handler Optional callback invoked for non-stop signals.
 * @param user_data Context provided to extra_handler.
 * @return MASK_OK on success, MASK_ERR on setup failure.
 */
int mask_reactor_watch_signals(struct mask_reactor *r, const int *signals, int nsignals,
                                mask_fd_handler extra_handler, void *user_data);

/**
 * @brief Creates a timerfd and registers it with the reactor.
 *
 * @param r Reactor instance.
 * @param interval_ms Timer period in milliseconds.
 * @param handler Callback for expiration events.
 * @param user_data User data passed to the callback.
 * @param out_fd Optional output fd pointer to retain the timer descriptor.
 * @return MASK_OK on success, MASK_ERR otherwise.
 */
int mask_reactor_add_timer(struct mask_reactor *r, int interval_ms,
                            mask_fd_handler handler, void *user_data, int *out_fd);

/**
 * @brief Rearms an existing timerfd with a new interval.
 *
 * @param timer_fd Timer fd created by mask_reactor_add_timer().
 * @param interval_ms New period in milliseconds.
 * @return MASK_OK on success, MASK_ERR on failure.
 */
int mask_reactor_rearm_timer(int timer_fd, int interval_ms);

#endif /* MASK_REACTOR_H */
