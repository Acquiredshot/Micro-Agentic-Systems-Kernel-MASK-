# MASK Architecture

This document captures the low-level control flow of the daemon and the key data structures that keep the system deterministic and resource-bounded.

## Execution model

MASK is built around a single event loop with a short-lived worker model. The reactor thread owns the file-descriptor event loop, signal handling, and periodic ticking, while worker threads handle LLM calls and bounded tool execution.

The intended state progression is:

```text
[Memory Buffer]
      ↓
[Inference Engine]
      ↓
[Function Dispatch]
      ↓
[POSIX Execution]
      ↓
[Observation Buffer]
```

In implementation terms, that flow maps to:

- `mask_ring_buffer_push()` stores observations in a fixed ring buffer.
- `llm_worker_main()` reads the recent snapshot and composes a prompt.
- `mask_tool_gateway_dispatch()` resolves a tool by name using function pointers.
- `mask_sandbox_exec()` runs the command in a child process via `fork` + `execvp`.
- The tool result and execution log are written back into the ring buffer for the next reasoning cycle.

## Core components

### 1. Reactor and scheduler

The main event loop is implemented in `src/reactor.c` and exposed through `include/mask/reactor.h`.

Responsibilities:

- register fd handlers in a bounded table,
- monitor socket readiness with `epoll`,
- dispatch timer and signal events,
- stop cleanly on `SIGINT` / `SIGTERM`.

The event loop is intentionally small enough that the daemon can reason about all state transitions without a large framework or runtime.

### 2. Memory buffer

The short-term working memory is the ring buffer in `src/ring_buffer.c` and `include/mask/ring_buffer.h`.

```c
struct mask_memory_entry {
    uint64_t timestamp_ms;
    char role[MASK_MEMORY_ROLE_MAX];
    char text[MASK_MEMORY_TEXT_MAX];
};
```

This design keeps each entry compact and predictable:

- `timestamp_ms` gives ordering for records,
- `role` identifies the source (`sysinfo`, `llm`, `tool`, `system`, etc.),
- `text` stores sanitized output up to `MASK_MEMORY_TEXT_MAX - 1` bytes.

The ring buffer is fixed-capacity and uses a mutex to synchronize producers and snapshots. Once it fills, the oldest records are overwritten instead of growing indefinitely.

### 3. Inference engine

The LLM integration lives in `src/llm_client.c` and `src/main.c`.

The worker thread:

1. snapshots the recent ring buffer,
2. builds the prompt including the tool manifest,
3. sends the prompt to the Ollama-compatible endpoint,
4. parses the LLM output as JSON when possible.

The daemon intentionally reduces the lifetime of inference data to a bounded snapshot, preventing the event loop from being blocked on long network I/O.

### 4. Function dispatch and tool registry

The tool registry is defined in `include/mask/tool_gateway.h`.

```c
struct mask_tool {
    char name[MASK_TOOL_NAME_MAX];
    char description[MASK_TOOL_DESC_MAX];
    mask_tool_fn fn;
};
```

The dispatch table is a fixed array of function pointers. Each tool is registered once during startup, and the LLM manifest is generated from those entries. This avoids dynamic plugin loading and keeps tool discovery explicit and auditable.

### 5. POSIX execution sandbox

For process execution, MASK uses `fork` + `execvp` in `src/sandbox.c` rather than `popen`.

That choice is deliberate:

- `popen()` invokes a shell by default, expanding the attack surface for shell metacharacters and quoting bugs.
- `execvp()` lets the code pass an already-validated `argv[]` vector directly to the target program.
- The child process can immediately apply resource limits before launching the command.

The sandbox path applies `setrlimit()` for CPU time, memory, file size, open file count, and process count, then redirects `stdin` to `/dev/null` so interactive commands cannot block the execution path.

The parent process tracks the child with `poll()` and a wall-clock timeout. If the command exceeds the allowed time, it is terminated with `SIGKILL`.

### 6. Observation flow

The final stage is writing the result back into the ring buffer. Observations may include:

- system metrics from `sysinfo`,
- LLM responses,
- tool calls and their exit codes,
- system notices related to shutdown or failures.

This makes the daemon’s memory and the dashboard share the same event vocabulary, with each record carrying a role and timestamp.

## Memory and buffer layout

The key fixed limits are:

- `MASK_MAX_TOOLS` = 32
- `MASK_TOOL_NAME_MAX` = 64
- `MASK_TOOL_DESC_MAX` = 256
- `MASK_MEMORY_TEXT_MAX` = 512
- `MASK_MEMORY_ROLE_MAX` = 16

These sizes are intentionally conservative so the daemon remains predictable under load. If a feature grows beyond these bounds, the correct path is usually a new explicit data structure or a more precise smaller output format, not a silent increase in allocation.

## Process and signal strategy

Signal handling and process lifecycle are coordinated as follows:

1. `mask_reactor_watch_signals()` installs `signalfd` listeners.
2. `SIGINT` and `SIGTERM` cause the reactor loop to stop.
3. The daemon waits for any in-flight LLM worker to finish.
4. A sandboxed child is terminated if it exceeds its wall-clock or CPU budget.

This pattern avoids ad hoc signal handling in worker threads and keeps shutdown decisions centralized in the event loop.

## Design constraints

MASK is intentionally opinionated:

- no hidden dynamic plugin system,
- no shell execution in the LLM tool path,
- no unbounded log accumulation,
- no blocking I/O in the reactor loop,
- no implicit dependency on a large runtime or garbage collector.

Those constraints are the main reason the project can stay small, inspectable, and stable in a native Linux environment.

## Extension points

When adding a new subsystem, follow the same pattern:

- keep the state in a fixed-size, explicit structure,
- isolate non-reactor work in a worker thread,
- add the tool to the registry rather than creating a separate dynamic dispatch path,
- store observations in the ring buffer so the full system can reason over them.

This keeps the architecture readable and keeps the codebase aligned with real-world C systems engineering practices.
