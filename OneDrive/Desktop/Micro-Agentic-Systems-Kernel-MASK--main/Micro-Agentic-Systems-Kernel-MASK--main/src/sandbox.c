#include "mask/sandbox.h"
#include "mask/common.h"

#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

/**
 * @file sandbox.c
 * @brief POSIX process sandbox used to run LLM-triggered tool commands safely.
 */

void mask_sandbox_default_limits(struct mask_sandbox_limits *limits) {
    limits->cpu_seconds = 5;
    limits->address_space_mb = 256;
    limits->file_size_mb = 16;
    limits->max_open_files = 32;
    limits->wall_timeout_sec = 10;
}

static void apply_rlimit(int resource, rlim_t value) {
    struct rlimit rl;
    rl.rlim_cur = value;
    rl.rlim_max = value;
    /* Best-effort: a restricted parent (e.g. already namespaced) may not be
     * able to raise limits, but can always lower them, which is our use case. */
    setrlimit(resource, &rl);
}

/* Runs in the child after fork(), before execvp(). Must only call
 * async-signal-safe functions. */
static void child_apply_limits(const struct mask_sandbox_limits *limits) {
    if (limits->cpu_seconds > 0) {
        apply_rlimit(RLIMIT_CPU, (rlim_t)limits->cpu_seconds);
    }
    if (limits->address_space_mb > 0) {
        apply_rlimit(RLIMIT_AS, (rlim_t)limits->address_space_mb * 1024 * 1024);
    }
    if (limits->file_size_mb > 0) {
        apply_rlimit(RLIMIT_FSIZE, (rlim_t)limits->file_size_mb * 1024 * 1024);
    }
    if (limits->max_open_files > 0) {
        apply_rlimit(RLIMIT_NOFILE, (rlim_t)limits->max_open_files);
    }
    /* Never allow the child to spawn further children indefinitely. */
    apply_rlimit(RLIMIT_NPROC, 16);
}

int mask_sandbox_exec(char *const argv[], const struct mask_sandbox_limits *limits,
                       char *output, size_t output_size) {
    struct mask_sandbox_limits default_limits;
    if (!limits) {
        mask_sandbox_default_limits(&default_limits);
        limits = &default_limits;
    }

    if (output && output_size > 0) {
        output[0] = '\0';
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        MASK_LOGE("pipe() failed: %s", strerror(errno));
        return MASK_ERR;
    }

    pid_t pid = fork();
    if (pid < 0) {
        MASK_LOGE("fork() failed: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return MASK_ERR;
    }

    if (pid == 0) {
        /* Child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        /* Never let a sandboxed command inherit our stdin: an interactive
         * program (a bare shell, a pager, anything expecting a TTY) would
         * otherwise block reading from it until the wall-clock timeout. */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        child_apply_limits(limits);

        execvp(argv[0], argv);
        /* execvp only returns on failure */
        _exit(127);
    }

    /* Parent */
    close(pipefd[1]);

    size_t used = 0;
    int timed_out = 0;
    uint64_t deadline_ms = mask_now_ms() + (uint64_t)limits->wall_timeout_sec * 1000ULL;

    for (;;) {
        int remaining_ms = (int)(deadline_ms - mask_now_ms());
        if (remaining_ms < 0) {
            remaining_ms = 0;
        }

        struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
        int pr = poll(&pfd, 1, remaining_ms);

        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (pr == 0) {
            timed_out = 1;
            break;
        }

        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            char buf[512];
            ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n <= 0) {
                break; /* EOF or error: child closed its end */
            }
            if (output && used + 1 < output_size) {
                size_t space = output_size - 1 - used;
                size_t copy = (size_t)n < space ? (size_t)n : space;
                memcpy(output + used, buf, copy);
                used += copy;
                output[used] = '\0';
            }
        }
    }

    if (timed_out) {
        kill(pid, SIGKILL);
    }

    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (timed_out) {
        MASK_LOGW("sandboxed command timed out after %d s, killed", limits->wall_timeout_sec);
        return -128 - SIGKILL;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return -128 - WTERMSIG(status);
    }
    return MASK_ERR;
}
