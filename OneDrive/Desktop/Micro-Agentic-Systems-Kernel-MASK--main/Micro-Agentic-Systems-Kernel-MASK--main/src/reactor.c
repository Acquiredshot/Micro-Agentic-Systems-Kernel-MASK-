#include "mask/reactor.h"
#include "mask/common.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#define MASK_MAX_EPOLL_EVENTS 32

struct mask_signal_ctx {
    mask_fd_handler extra_handler;
    void *user_data;
    struct mask_reactor *reactor;
};

/* Fixed arena for the small amount of bookkeeping state the convenience
 * wrappers (signal/timer) need to keep alive for the process lifetime. */
#define MASK_MAX_SIGNAL_CTX 4
static struct mask_signal_ctx g_signal_ctx_pool[MASK_MAX_SIGNAL_CTX];
static size_t g_signal_ctx_used = 0;

int mask_reactor_init(struct mask_reactor *r) {
    memset(r, 0, sizeof(*r));
    r->epfd = epoll_create1(0);
    if (r->epfd < 0) {
        MASK_LOGE("epoll_create1 failed: %s", strerror(errno));
        return MASK_ERR;
    }
    r->running = 1;
    return MASK_OK;
}

void mask_reactor_destroy(struct mask_reactor *r) {
    if (r->epfd >= 0) {
        close(r->epfd);
        r->epfd = -1;
    }
}

static struct mask_fd_entry *find_free_slot(struct mask_reactor *r) {
    for (size_t i = 0; i < MASK_REACTOR_MAX_HANDLERS; i++) {
        if (!r->handlers[i].in_use) {
            return &r->handlers[i];
        }
    }
    return NULL;
}

int mask_reactor_add_fd(struct mask_reactor *r, int fd, uint32_t events,
                         mask_fd_handler handler, void *user_data) {
    struct mask_fd_entry *slot = find_free_slot(r);
    if (!slot) {
        MASK_LOGE("reactor handler table full (max %d)", MASK_REACTOR_MAX_HANDLERS);
        return MASK_ERR;
    }

    slot->fd = fd;
    slot->in_use = 1;
    slot->handler = handler;
    slot->user_data = user_data;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = slot;

    if (epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
        MASK_LOGE("epoll_ctl ADD failed for fd %d: %s", fd, strerror(errno));
        slot->in_use = 0;
        return MASK_ERR;
    }

    return MASK_OK;
}

int mask_reactor_remove_fd(struct mask_reactor *r, int fd) {
    for (size_t i = 0; i < MASK_REACTOR_MAX_HANDLERS; i++) {
        if (r->handlers[i].in_use && r->handlers[i].fd == fd) {
            epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, NULL);
            r->handlers[i].in_use = 0;
            return MASK_OK;
        }
    }
    return MASK_ERR;
}

int mask_reactor_run(struct mask_reactor *r) {
    struct epoll_event events[MASK_MAX_EPOLL_EVENTS];

    while (r->running) {
        int n = epoll_wait(r->epfd, events, MASK_MAX_EPOLL_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            MASK_LOGE("epoll_wait failed: %s", strerror(errno));
            return MASK_ERR;
        }

        for (int i = 0; i < n; i++) {
            struct mask_fd_entry *slot = (struct mask_fd_entry *)events[i].data.ptr;
            if (slot && slot->in_use && slot->handler) {
                slot->handler(r, slot->fd, events[i].events, slot->user_data);
            }
        }
    }

    return MASK_OK;
}

void mask_reactor_stop(struct mask_reactor *r) {
    r->running = 0;
}

static void signal_fd_handler(struct mask_reactor *r, int fd, uint32_t events, void *user_data) {
    (void)events;
    struct mask_signal_ctx *ctx = (struct mask_signal_ctx *)user_data;
    struct signalfd_siginfo fdsi;

    ssize_t n = read(fd, &fdsi, sizeof(fdsi));
    if (n != sizeof(fdsi)) {
        return;
    }

    int signum = (int)fdsi.ssi_signo;
    if (signum == SIGINT || signum == SIGTERM) {
        MASK_LOGI("received signal %d, shutting down", signum);
        mask_reactor_stop(r);
        return;
    }

    if (ctx->extra_handler) {
        ctx->extra_handler(r, fd, events, ctx->user_data);
    }
}

int mask_reactor_watch_signals(struct mask_reactor *r, const int *signals, int nsignals,
                                mask_fd_handler extra_handler, void *user_data) {
    if (g_signal_ctx_used >= MASK_MAX_SIGNAL_CTX) {
        MASK_LOGE("signal context pool exhausted");
        return MASK_ERR;
    }

    sigset_t mask;
    sigemptyset(&mask);
    for (int i = 0; i < nsignals; i++) {
        sigaddset(&mask, signals[i]);
    }

    if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0) {
        MASK_LOGE("sigprocmask failed: %s", strerror(errno));
        return MASK_ERR;
    }

    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sfd < 0) {
        MASK_LOGE("signalfd failed: %s", strerror(errno));
        return MASK_ERR;
    }

    struct mask_signal_ctx *ctx = &g_signal_ctx_pool[g_signal_ctx_used++];
    ctx->extra_handler = extra_handler;
    ctx->user_data = user_data;
    ctx->reactor = r;

    if (mask_reactor_add_fd(r, sfd, EPOLLIN, signal_fd_handler, ctx) != MASK_OK) {
        close(sfd);
        return MASK_ERR;
    }

    return MASK_OK;
}

static int arm_timerfd(int tfd, int interval_ms) {
    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_value.tv_sec = interval_ms / 1000;
    its.it_value.tv_nsec = (interval_ms % 1000) * 1000000L;
    its.it_interval = its.it_value;

    if (timerfd_settime(tfd, 0, &its, NULL) != 0) {
        MASK_LOGE("timerfd_settime failed: %s", strerror(errno));
        return MASK_ERR;
    }
    return MASK_OK;
}

int mask_reactor_add_timer(struct mask_reactor *r, int interval_ms,
                            mask_fd_handler handler, void *user_data, int *out_fd) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) {
        MASK_LOGE("timerfd_create failed: %s", strerror(errno));
        return MASK_ERR;
    }

    if (arm_timerfd(tfd, interval_ms) != MASK_OK) {
        close(tfd);
        return MASK_ERR;
    }

    if (mask_reactor_add_fd(r, tfd, EPOLLIN, handler, user_data) != MASK_OK) {
        close(tfd);
        return MASK_ERR;
    }

    if (out_fd) {
        *out_fd = tfd;
    }
    return MASK_OK;
}

int mask_reactor_rearm_timer(int timer_fd, int interval_ms) {
    return arm_timerfd(timer_fd, interval_ms);
}
