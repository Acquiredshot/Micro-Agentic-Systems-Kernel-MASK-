#ifndef MASK_SANDBOX_H
#define MASK_SANDBOX_H

#include <stddef.h>

/* Resource limits applied to a sandboxed child before execvp(). All limits
 * are hard limits (RLIM); a value of 0 means "use the built-in default". */
struct mask_sandbox_limits {
    long cpu_seconds;     /* RLIMIT_CPU */
    long address_space_mb; /* RLIMIT_AS, in megabytes */
    long file_size_mb;    /* RLIMIT_FSIZE, in megabytes */
    int max_open_files;   /* RLIMIT_NOFILE */
    int wall_timeout_sec; /* killed via SIGKILL if it runs longer than this */
};

void mask_sandbox_default_limits(struct mask_sandbox_limits *limits);

/* Runs argv[0] with the given argv (NULL-terminated) in a forked child
 * under the given resource limits. Captures combined stdout+stderr into
 * output (truncated to output_size - 1, always NUL-terminated).
 *
 * Returns the child's exit status (0-255) on normal exit, or a negative
 * value if the process was killed by a signal or the sandbox itself
 * (e.g. MASK_ERR on fork/exec failure, -128 - signum if killed by signal).
 */
int mask_sandbox_exec(char *const argv[], const struct mask_sandbox_limits *limits,
                       char *output, size_t output_size);

#endif /* MASK_SANDBOX_H */
