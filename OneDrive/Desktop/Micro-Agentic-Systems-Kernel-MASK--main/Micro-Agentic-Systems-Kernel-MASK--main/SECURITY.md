# Security Considerations for MASK

MASK executes agent-driven commands in a constrained POSIX environment. The design goal is to keep the daemon predictable, resource-bounded, and resistant to accidental or malicious command expansion.

## Execution model

The command path does not use a shell wrapper. The project intentionally avoids `popen()` and instead executes tools through a `fork` + `execvp` pattern in the sandbox layer.

This matters because:

- `popen()` invokes a shell by default, which introduces shell metacharacter interpretation and quoting risks.
- `execvp()` accepts an already-prepared `argv[]` vector, which reduces command injection opportunities.
- The child process receives explicit resource caps before `execvp()` runs.

## Command sanitization and argument handling

### Preferred pattern

The project’s tool contract expects structured arguments, typically as JSON, and then converts them into an `argv[]`-style argument list.

Examples:

- `run_shell` expects an argument vector rather than a raw string command.
- The LLM should never be trusted to pass an unfiltered shell snippet.

### Required guardrails

- Treat all model-provided command input as untrusted.
- Do not concatenate user or model text into a shell command string.
- Prefer explicit argv arrays and fixed parameter mapping.
- Reject empty or malformed tool arguments before invoking `execvp()`.

## Resource limits

The sandbox uses `setrlimit()` to constrain the child process. The current implementation enforces:

- CPU time (`RLIMIT_CPU`)
- virtual memory / address space (`RLIMIT_AS`)
- file size (`RLIMIT_FSIZE`)
- open file count (`RLIMIT_NOFILE`)
- process count (`RLIMIT_NPROC`)

This prevents runaway commands from consuming excessive system resources or spawning unbounded child processes.

## Standard input isolation

Before `execvp()`, the child redirects `stdin` from `/dev/null`.

This is important because many interactive or terminal-oriented programs can block waiting for input. If an agent-supplied command expects a TTY or reads stdin, the process would otherwise stall until the wall-clock timeout expired.

## Output capture and timeout handling

The sandbox captures stdout/stderr through a pipe and monitors it with `poll()`. The parent process:

- reads output incrementally,
- enforces a wall-clock timeout,
- terminates the child with `SIGKILL` if it exceeds the limit.

This ensures a misbehaving tool cannot hang the daemon indefinitely.

## Process isolation recommendations

For production hardening, consider the following additional controls:

- run the daemon in a dedicated service account with the minimum required privileges,
- drop privileges in the child process before executing external commands when feasible,
- use Linux user namespaces or container isolation in higher-trust deployments,
- keep command execution inside an allowlist of approved binaries,
- log both the command argv and exit status for auditability.

## Security boundaries

The key boundary is between the reactor/LLM orchestration layer and the external command execution layer. The daemon should remain authoritative and conservative:

- the LLM decides intent,
- the daemon validates the tool call,
- the sandbox applies limits,
- the parent process captures and records outcome.

This layering is essential. The LLM should not be treated as a privileged executor.

## IPC Authentication

The IPC server supports optional token-based authentication to prevent unauthorized reconfiguration of daemon parameters:

- Set `MASK_IPC_SECRET` environment variable to enable authentication
- Clients must authenticate with `{"cmd":"auth","token":"<secret>"}` before administrative commands
- Administrative commands (`set_config`, `query_events`) require valid authentication
- Constant-time token comparison prevents timing attacks
- Backward compatible: authentication disabled if `MASK_IPC_SECRET` not set

## Tool Privilege System

The tool gateway enforces a three-tier privilege system:

- **SAFE**: Read-only diagnostics (sysinfo, net_connections, process_list) - no approval required
- **ELEVATED**: Network operations, file writes (file_read, file_write, net_check) - no approval required
- **RESTRICTED**: System modification, shell commands (run_shell, action) - requires manual approval

RESTRICTED tools require human approval before execution:
- Default state is PENDING (tool blocked)
- Operators approve via IPC or web interface
- Approval state tracked per tool invocation
- REJECTED state blocks execution explicitly

## Input Sanitization and RAG Guardrails

The daemon includes built-in guardrails for retrieval-augmented generation:

- **Intent Router**: Classifies inputs as QUERY, ACTION, DIAGNOSTIC, or UNKNOWN
- **Input Sanitizer**: Strips prompt injection tokens (case-insensitive pattern matching)
- **Context Bounding**: Limits context chunks to deterministic token sizes
- **Similarity Thresholding**: Rejects irrelevant chunks below threshold (default ≥0.75)
- **Loop Breakers**: Step counters and wall-clock timeouts prevent infinite loops
- **Output Redaction**: Regex-based redaction of sensitive data (IPs, tokens, paths, PII)

## Persistent Storage Security

The SQLite storage layer includes security considerations:

- Database file permissions should be restricted to daemon user only
- Input validation on all storage API parameters
- SQL injection protection via parameterized queries
- Automatic pruning of old records prevents unbounded growth
- Optional compilation - can be disabled if not needed

## Hardening checklist

Before a command is dispatched, confirm that:

- the requested tool is known and registered,
- the arguments are validated JSON or an explicit argv structure,
- the command vector is not built from raw shell text,
- the child process will run with reduced resource caps,
- stdin is isolated from terminal input,
- output is bounded to a safe fixed-size buffer,
- the low-level timeout exists and is enforced.

## Reporting issues

If you find a command-execution bug, privilege escalation issue, or sandbox bypass, report it privately and do not expose the exploit path publicly until it has been mitigated.
