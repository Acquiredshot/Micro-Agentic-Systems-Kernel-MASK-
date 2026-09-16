# Contributing to MASK

This project is a low-level C daemon, so changes should favor deterministic behavior, bounded memory, and explicit ownership over convenience abstractions.

## Development principles

- Prefer static allocation and fixed-size structures over dynamic growth in the daemon path.
- Keep the reactor loop non-blocking. Do not perform network I/O or long-running work directly on the epoll thread.
- Treat every buffer as bounded. If a value may exceed a fixed limit, truncate it intentionally and log the truncation.
- Use `fork` + `execvp` for sandboxed command execution; avoid wrappers such as `popen` when the goal is strict control over process limits and output capture.
- Preserve the invariant that a thread may read from a shared structure only if it is synchronized or if the data is copied into a thread-local snapshot first.

## Memory allocation and lifetime rules

### Fixed-size, explicit ownership

The daemon intentionally uses static arrays and one-time allocation for long-lived state:

- `struct mask_reactor.handlers[]` provides a fixed-size FD table.
- `struct mask_ring_buffer.entries` is allocated once at init and reused until shutdown.
- `struct mask_tool_gateway.tools[]` is a fixed table of tool descriptors.

Avoid introducing hidden allocations in the event loop or in per-tick code paths. If a feature needs a heap allocation, ensure it happens only in a worker thread and is released before the next state transition.

### Ring buffer constraints

The ring buffer is intentionally bounded to prevent runaway memory growth. Key rules:

- `MASK_MEMORY_TEXT_MAX` must remain small enough that a single log entry cannot dominate memory use.
- `mask_ring_buffer_push()` must copy and sanitize text into a pre-allocated slot; it must never append to an unbounded string.
- Snapshot callers must pass a destination buffer sized to the maximum recent entries they want to inspect.
- If you extend the entry schema, keep the total per-entry size predictable and update any tests that assume the old size.

### Tool and config strings

All names and descriptions are fixed-width strings. That means:

- `snprintf()` is required rather than `strcpy()`.
- Truncation is expected behavior for longer values.
- String fields read from external sources must be validated before use in the dispatch table or config updates.

## Threading and signal safety

### Reactor thread

The reactor loop owns file-descriptor readiness and signal delivery. It should keep work reasonably small and avoid blocking calls. Long-running operations belong in detached worker threads.

### Worker thread rules

Worker threads may:

- read a bounded snapshot from the ring buffer,
- call the LLM backend,
- dispatch one tool by name,
- push observations back into the ring buffer.

Worker threads must not directly mutate shared config or reactor state without a clear synchronization contract. The project pattern is to copy the needed values into a small job struct before the thread starts.

### Signal handling

Signal handling follows the project default:

- the reactor installs signalfd listeners for `SIGINT` and `SIGTERM`,
- shutdown is triggered via the reactor state machine,
- worker threads finish their current work before the daemon exits.

Do not add signal handlers that call malloc, printf, or non-async-signal-safe functions in the signal context.

## Adding a tool

New system tools belong in the tool registry and must be added in the same pattern as the existing `sysinfo` and `run_shell` implementations.

1. Implement the tool function with this signature:

   ```c
   int tool_name(const char *args_json, char *output, size_t output_size);
   ```

2. Add the implementation file under `src/tools/`.

3. Register it in the daemon startup path, typically in `src/main.c`:

   ```c
   mask_tool_gateway_register(&daemon.tools, "tool_name",
       "Human-readable description for the LLM manifest.", tool_name);
   ```

4. Ensure the function writes JSON or plain text into `output` without exceeding `output_size`.

5. Add a smoke test if the tool has a distinct behavior or failure mode.

The dispatch model is intentionally simple: the LLM replies with a JSON object containing a tool name and arguments, and `mask_tool_gateway_dispatch()` resolves the function pointer from the fixed registry.

## Code review expectations

When preparing a patch, review the following before opening a PR:

- Is the memory footprint still bounded?
- Does the code avoid blocking the reactor thread?
- Are all fixed-size arrays and buffer lengths respected?
- Are thread-safety boundaries preserved?
- Are failures handled without leaking descriptors or leaving the process in a partial state?

## Local validation

Run the project’s focused testing target:

```bash
make test
```

For broader validation in a sanitizer-enabled build, use the commands described in the README before submitting a change that touches scheduling, process execution, or memory layout.

## Documentation expectations

When altering low-level behavior, update the relevant design notes in this repository:

- [`README.md`](README.md)
- [`ARCHITECTURE.md`](ARCHITECTURE.md)
- any relevant header comments that feed Doxygen output

This keeps the implementation and the engineering description aligned as the system evolves.
