# Changelog

All notable changes to MASK are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/):
entries are grouped under `Added` / `Changed` / `Fixed` / `Removed` per
version. Undated `[Unreleased]` collects work not yet cut into a version.

## [Unreleased]

### Added
- Engineering-focused repository documentation for a native C systems project:
  - [`CONTRIBUTING.md`](CONTRIBUTING.md) with memory discipline, thread-safety rules, and tool-registration guidance.
  - [`ARCHITECTURE.md`](ARCHITECTURE.md) documenting the event-loop state flow, ring-buffer layout, sandbox process model, and signal lifecycle.
  - [`SECURITY.md`](SECURITY.md) covering command sanitization, `setrlimit` protections, and process isolation guidelines.
  - [`Doxyfile`](Doxyfile) for generating API docs with Doxygen from the public C interfaces.
- Doxygen-style API comments added to the public headers and sandbox implementation to improve generated documentation quality and maintainability.
- Modern remote web control panel for system monitoring, alert review, and fleet visibility.
- Browser-based operational oversight so admins can manage and inspect the daemon without direct on-prem terminal access.
- Remote administration workflows for monitoring, diagnostics, and status review across multiple endpoints.
- Product packaging and commercial positioning updates around a tiered SaaS microservice model.
- **Live config control** (the dashboard can now act on the daemon, not just observe it):
  - New IPC commands in `src/ipc.c`: `get_config` (read current live-tunable
    settings) and `set_config` (change them at runtime, validated
    all-or-nothing before anything is applied). `tick_interval_ms`,
    `llm_every_n_ticks`, `paused`, `llm_endpoint`, and `llm_model` can all be
    changed without restarting `maskd`. The `snapshot` reply now embeds the
    current config too.
  - `mask_reactor_rearm_timer()` in `reactor.c`/`reactor.h`: re-arms an
    existing `timerfd`'s period in place, so changing `tick_interval_ms`
    takes effect immediately instead of needing a restart.
  - `bridge/server.js` relays `set_config`/`get_config` WebSocket messages
    from a browser client to the daemon and back, and forces an immediate
    re-poll after a successful change so every connected client sees the
    new state right away instead of waiting for the next scheduled poll.
  - `web/src/components/ControlPanel.tsx`: a form for tick interval,
    reasoning-cycle cadence, pause/resume, and LLM endpoint/model, with a
    local "draft" that only syncs from the live daemon state when the user
    isn't mid-edit, and inline error display when the daemon rejects a value.

### Fixed
- A real data race introduced by making config live-tunable: the LLM
  worker thread used to read `cfg.llm_endpoint`/`cfg.llm_model` directly
  from the shared daemon struct throughout its run, which is unsafe once
  those fields can be mutated concurrently by an IPC `set_config` command
  on the reactor thread. Fixed by snapshotting both strings into a
  per-job struct at the moment the worker is spawned (still on the
  reactor thread, so the snapshot itself is race-free) instead of reading
  the shared struct from the worker thread.

### Changed
- Reframed the product as an AI operations service with a web control plane, not just a local daemon with command tools.
- Aligned the product narrative to emphasize remote monitoring, multi-system control, and managed-service readiness.

### Notes
- The web dashboard remains a key product differentiator for SMEs and B2B buyers who need remote operational visibility and reduced on-site dependency.
- Verified live: `set_config` correctly re-arms the tick timer (confirmed
  tick counter advancing at the new rate) and `paused` correctly stops new
  reasoning cycles (confirmed via daemon log — no further LLM attempts
  after pausing, across a 10+ second window that would otherwise have
  triggered several); invalid input (e.g. `tick_interval_ms` out of range)
  is rejected with no partial changes applied. The full path was also
  exercised through the actual `ControlPanel` UI (toggling "paused" via
  the browser and confirming the change against the daemon directly,
  bypassing the UI, both before and after).

## [0.2.0] - 2026-09-15

Added a web-based dashboard on top of the daemon, and along the way fixed a
real bug, hardened the ring buffer, and switched the IPC transport to
support a client running outside the daemon's own container.

### Added
- IPC server (`src/ipc.c`, `include/mask/ipc.h`): a TCP server on the
  reactor exposing a `snapshot` request (tick count, `llm_busy`, recent
  ring-buffer entries, tool manifest) as JSON, one reply per connection.
  `MASK_IPC_PORT` (default `7717`).
- `bridge/`: a small Node.js WebSocket server that polls the daemon's IPC
  endpoint and broadcasts snapshots to browser clients, plus a rolling
  history buffer of `sysinfo` samples for charting (the daemon's own ring
  buffer is short-term and bounded; long-running analytics live here).
- `web/`: a Vite + React + TypeScript + Tailwind dashboard (Recharts for
  charts) — live connection/reasoning status, load-average and
  memory-usage time series, the registered tool list, and a color-coded,
  auto-scrolling event log. Chart colors and layout follow the project's
  dataviz guidelines: fixed-order CVD-safe categorical palette for the
  three load-average series, single-hue wash for the memory area chart,
  hover tooltips, dark-mode-only surfaces.
- Per-cycle logging in `main.c` (`LLM reply: ...`, `dispatched tool ...`,
  `LLM chose no tool this cycle`) so reasoning cycles are observable in the
  daemon's own log output, not just inferred from tool side effects.
- `LICENSE`: MIT license (copyright CodyCodesIT).

### Changed
- **IPC transport switched from a Unix domain socket to TCP loopback**
  (`127.0.0.1:7717`). A Unix socket can't be reached from outside the
  daemon's own filesystem namespace; TCP loopback works both for local
  clients and for a client on Windows talking to a `maskd` running in
  WSL2, since WSL2 auto-forwards loopback TCP ports but not Unix sockets.
- `ring_buffer.c` now sanitizes stored text: strips ANSI escape sequences
  (CSI/OSC) and other control bytes, keeping `\n`/`\t`. Previously a
  sandboxed command like `clear` would leave raw escape codes in any
  consumer's view of the ring buffer.

### Removed
- The native Dear ImGui desktop frontend (previously `gui/`,
  `third_party/imgui/`) and its Windows build path (MinGW-w64 + prebuilt
  GLFW). It worked — including as a genuine native Win32 build after
  WSLg's RDP window forwarding proved unreliable for an OpenGL app on this
  multi-monitor setup — but the web dashboard superseded it as the
  intended frontend, and maintaining two UIs wasn't worth it.

### Verified
- End-to-end run against a real local Ollama server (`llama3.2:latest`):
  the daemon correctly requests `sysinfo`, dispatches `run_shell` (`echo`,
  `cat`, `ls`, `df`), and handles no-tool-needed commentary replies, all
  from live model output.
- AddressSanitizer + UndefinedBehaviorSanitizer: ~20 reasoning cycles
  against live Ollama with `MASK_RING_CAPACITY=8` (forcing repeated ring
  buffer wraparound) and `detect_leaks=1` — zero errors, zero leaks.
- ThreadSanitizer: ~25 reasoning cycles at `MASK_TICK_MS=1000` (maximum
  reactor/worker-thread contention) — zero data races reported.
- Malformed-output resilience: invalid JSON from the model (e.g. a broken
  `run_shell` argv array) fails `cJSON_Parse` cleanly and is logged as
  commentary instead of crashing or dispatching a garbage tool call.
- Signal handling: `SIGTERM` and `SIGINT` both trigger a clean shutdown
  with no orphaned children.
- Full dashboard pipeline (`maskd` -> `bridge/` -> `web/`) verified live
  in a real browser, including chart data accumulating correctly across
  daemon reasoning cycles.

## [0.1.0] - 2026-09-15

Initial skeleton. Every core architectural component exists, compiles clean
(`-Wall -Wextra`, zero warnings), and has been exercised end to end in WSL2
Ubuntu.

### Added
- Reactor (`src/reactor.c`): `epoll`-based event loop, fixed-size handler
  table, `signalfd` signal handling, `timerfd` periodic ticks.
- Ring buffer (`src/ring_buffer.c`): fixed-arena, mutex-protected short-term
  memory, single allocation at init, oldest-entry overwrite on wrap.
- Tool gateway (`src/tool_gateway.c`): function-pointer dispatch table with
  JSON manifest generation.
- LLM client (`src/llm_client.c`): `libcurl` + `cJSON` client for an
  Ollama-compatible `/api/generate` endpoint, with a size-capped response
  buffer.
- Sandbox (`src/sandbox.c`): `fork`/`execvp` execution with `setrlimit`
  (CPU, address space, file size, open files, process count) and a
  `poll`-based wall-clock timeout.
- Two example tools: `sysinfo` (`/proc/loadavg`, `/proc/meminfo`) and
  `run_shell` (sandboxed argv-array execution, no shell involved).
- Daemon glue (`src/main.c`): per-tick sysinfo sampling, periodic LLM
  reasoning cycles on detached worker threads so the reactor never blocks
  on network I/O, graceful shutdown that waits for an in-flight worker.
- Vendored `cJSON` under `third_party/cjson/` (MIT, from the official repo).
- `Makefile` with `all`, `run`, `test`, `clean` targets.
- Standalone smoke test (`tests/smoke_tools.c`) covering tool dispatch,
  successful and failing sandboxed execution, and `RLIMIT_CPU` enforcement.
- `README.md` documenting architecture, build/run instructions, and known
  limitations.

### Known limitations (tracked, not yet fixed)
- Tool-call parsing expects bare JSON from the model; no tolerance for
  prose-wrapped or markdown-fenced replies.
- No capability policy — any registered tool is callable with no
  allow/deny rules beyond sandbox resource limits.
- No persistent memory across restarts.
