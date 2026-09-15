# MASK — Micro Agentic Systems Kernel

A native, event-driven C daemon that acts as an autonomous system controller:
it watches low-level system metrics, talks to a local LLM inference backend
(Ollama-style HTTP API), executes tools in response, and keeps a bounded,
thread-safe memory of what it's seen — paired with a modern web dashboard
for observability.

No Python, no Node, no heap-happy abstractions in the daemon itself. Just
`epoll`, `libcurl`, and disciplined C. The dashboard is a separate, optional
layer on top.

## Why

Most "agent" runtimes are a scripting language wrapping HTTP calls. MASK
inverts that: the daemon *is* the reactor. It never blocks its event loop on
network I/O, never grows memory without bound, and never lets an agent-issued
command run outside a resource-limited sandbox. It's meant to be small enough
to read end to end in one sitting.

## Architecture

```
┌─────────────┐  TCP/JSON   ┌──────────────┐  WebSocket   ┌──────────────────┐
│    maskd    │◄───────────►│ bridge/      │◄────────────►│  web/ (React)    │
│  (C daemon) │  127.0.0.1  │ (Node.js)    │              │  Vite + Tailwind │
└─────────────┘             └──────────────┘              │  + Recharts      │
                                                            └──────────────────┘
```

| Component | Files | Responsibility |
|---|---|---|
| **Reactor** | [`src/reactor.c`](src/reactor.c) | `epoll`-based event loop. Registers fds against a fixed-size handler table (no per-fd allocation). Provides `signalfd`-based POSIX signal handling and `timerfd`-based periodic ticks. |
| **Ring buffer** | [`src/ring_buffer.c`](src/ring_buffer.c) | Fixed-arena, mutex-protected short-term memory. One `calloc` at init; oldest entries are overwritten once full. Sanitizes stored text (strips ANSI escape sequences and other control bytes) so every consumer gets plain printable text. |
| **Tool gateway** | [`src/tool_gateway.c`](src/tool_gateway.c) | Zero-dependency dispatcher: a table of `{name, description, function pointer}`. Builds a JSON manifest of available tools for the LLM prompt, and dispatches by name. |
| **LLM client** | [`src/llm_client.c`](src/llm_client.c) | `libcurl` + `cJSON` client for an Ollama-compatible `/api/generate` endpoint. Response body is read into a size-capped growable buffer so a misbehaving endpoint can't exhaust memory. |
| **Sandbox** | [`src/sandbox.c`](src/sandbox.c) | Runs a tool-requested command via `fork` + `execvp`, never a shell. Applies `setrlimit` (CPU time, address space, file size, open files, process count), redirects the child's stdin to `/dev/null` (so an interactive command can't hang it), and enforces a wall-clock timeout via `poll`. |
| **IPC server** | [`src/ipc.c`](src/ipc.c) | TCP server on `127.0.0.1` (loopback only), registered on the same reactor. A client connects, sends one line of JSON, gets one JSON reply, and the connection closes. Three commands: `{"cmd":"snapshot"}` (tick count, `llm_busy`, current config, recent ring-buffer entries, tool manifest), `{"cmd":"get_config"}`, and `{"cmd":"set_config","config":{...partial...}}` to change `tick_interval_ms`, `llm_every_n_ticks`, `paused`, `llm_endpoint`, or `llm_model` live, without restarting the daemon — validated all-or-nothing before anything is applied. TCP rather than a Unix socket specifically so a client can run outside the daemon's own container/VM (e.g. a dashboard on Windows against a daemon in WSL2, which auto-forwards loopback TCP ports but not Unix sockets). |
| **Daemon glue** | [`src/main.c`](src/main.c) | Wires the above together: each tick samples system info into the ring buffer; every *N* ticks it spawns a detached worker thread that snapshots memory, calls the LLM, and dispatches any tool call the model requests — so the reactor's `epoll_wait` loop never blocks on the network. |

### Included tools

- **`sysinfo`** ([`src/tools/tool_sysinfo.c`](src/tools/tool_sysinfo.c)) — reads `/proc/loadavg` and `/proc/meminfo`, returns them as JSON. No arguments.
- **`run_shell`** ([`src/tools/tool_shell.c`](src/tools/tool_shell.c)) — runs a sandboxed command. Takes `{"argv":["cmd","arg1",...]}` — an argv array, never a raw command string, so there is no shell-metacharacter injection surface.

### Tool-call convention

The system prompt sent to the LLM lists the tool manifest and asks for a
reply shaped like:

```json
{"tool": "sysinfo", "args": {}}
```

or, when no action is needed:

```json
{"tool": null, "say": "everything looks nominal"}
```

This is a minimal convention, not a real function-calling protocol — see
[Known limitations](#known-limitations).

## The dashboard

Two small pieces sit on top of the daemon, entirely optional:

- **[`bridge/`](bridge/)** — a small Node.js WebSocket server. Polls `maskd`'s
  TCP IPC endpoint on an interval (default 500ms) and broadcasts each
  snapshot to every connected browser client. It also accumulates a rolling
  history of `sysinfo` samples (load average, memory) for charting, since
  the daemon's own ring buffer is short-term and bounded — long-running
  analytics live in the bridge, not the C core.
- **[`web/`](web/)** — a Vite + React + TypeScript + Tailwind dashboard.
  Connects to the bridge over a WebSocket, and renders live status, load
  average / memory charts (Recharts), the registered tool list, a
  color-coded auto-scrolling event log, and a **control panel** — tick
  interval, reasoning-cycle cadence, pause/resume, and LLM endpoint/model
  can all be changed live from the browser, relayed through the bridge to
  the daemon's `set_config` IPC command.

Neither piece is required to run the daemon — `maskd` works standalone with
just the IPC server listening for whoever wants to poll it (e.g. with
`nc 127.0.0.1 7717`):

```
> {"cmd":"get_config"}
< {"ok":true,"config":{"tick_interval_ms":5000,"llm_every_n_ticks":6,"paused":false,"llm_endpoint":"http://127.0.0.1:11434","llm_model":"llama3.2"}}

> {"cmd":"set_config","config":{"paused":true}}
< {"ok":true,"config":{"tick_interval_ms":5000,"llm_every_n_ticks":6,"paused":true,"llm_endpoint":"http://127.0.0.1:11434","llm_model":"llama3.2"}}
```

## Requirements

**Daemon:**
- Linux (uses `epoll`, `signalfd`, `timerfd`, `/proc`) — developed and tested
  under WSL2 Ubuntu, but there is nothing WSL-specific in the code.
- `gcc` (or another C11-capable compiler) and `make`
- `libcurl` development headers (`libcurl4-openssl-dev` on Debian/Ubuntu)
- An Ollama-compatible inference server (optional at build time; only needed
  at runtime for the LLM reasoning cycle to do anything useful)

**Dashboard (optional):**
- Node.js 18+ and npm, for both `bridge/` and `web/`
- Can run anywhere that can reach the daemon's IPC port — including natively
  on Windows against a `maskd` running in WSL2, since WSL2 auto-forwards
  loopback TCP ports.

`cJSON` is vendored under [`third_party/cjson/`](third_party/cjson/) (MIT
licensed, pulled from the [official repo](https://github.com/DaveGamble/cJSON))
so there's no separate install step for it.

## Building and running

### Daemon

```bash
sudo apt-get update && sudo apt-get install -y build-essential libcurl4-openssl-dev
make
./maskd
```

By default it points at `http://127.0.0.1:11434` (a local Ollama instance)
with model `llama3.2`, ticking every 5 seconds and running an LLM reasoning
cycle every 6th tick (~30s), with its IPC server on `127.0.0.1:7717`.
Override any of this with environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `MASK_LLM_ENDPOINT` | `http://127.0.0.1:11434` | Base URL of the Ollama-compatible endpoint |
| `MASK_LLM_MODEL` | `llama3.2` | Model name passed in each request |
| `MASK_TICK_MS` | `5000` | Milliseconds between reactor ticks |
| `MASK_LLM_EVERY_N_TICKS` | `6` | Run an LLM reasoning cycle every N ticks |
| `MASK_RING_CAPACITY` | `256` | Number of memory entries the ring buffer holds |
| `MASK_IPC_PORT` | `7717` | TCP port the IPC server listens on (127.0.0.1 only) |

Stop it with `Ctrl+C` (`SIGINT`) or `SIGTERM` — it waits for any in-flight
LLM worker thread to finish before exiting.

If Ollama runs natively on Windows (not inside WSL) while `maskd` runs under
WSL2, you'll need Ollama listening on all interfaces, since WSL2's NAT
networking can't reach Windows' `127.0.0.1` directly:

```powershell
# Windows side: make Ollama listen on 0.0.0.0, then restart it
setx OLLAMA_HOST "0.0.0.0:11434"
```

```bash
# WSL side: find the gateway IP and point MASK at it
ip route show default   # "default via <gateway-ip> dev eth0" -- that's your Windows host
MASK_LLM_ENDPOINT=http://<gateway-ip>:11434 ./maskd
```

### Dashboard

With `maskd` already running:

```bash
cd bridge && npm install && npm start
```

```bash
cd web && npm install && npm run dev
```

Then open the URL Vite prints (typically `http://localhost:5173`). The
bridge defaults to polling the daemon at `127.0.0.1:7717` and serving its
own WebSocket on `ws://localhost:8765`; both are overridable via
`MASK_IPC_HOST`/`MASK_IPC_PORT`/`BRIDGE_PORT` env vars on the bridge, and
`VITE_BRIDGE_URL` on the web app.

This works whether the bridge and web app run inside WSL alongside the
daemon, or natively on Windows against a `maskd` in WSL2 — WSL2 forwards
loopback TCP ports automatically, so `127.0.0.1:7717` resolves correctly
either way.

## Testing

```bash
make test
```

Runs [`tests/smoke_tools.c`](tests/smoke_tools.c), a standalone harness that
exercises the tool gateway, sandbox, and ring buffer sanitization directly:
manifest generation, `sysinfo` output, a successful sandboxed `echo`, a
missing-binary failure, an unknown-tool lookup, `RLIMIT_CPU` actually killing
a busy-loop, and ANSI-escape stripping. It does not require an LLM endpoint.

### Sanitizer runs

For deeper correctness checks against a live Ollama endpoint, build with
GCC's sanitizers instead of the plain `Makefile` target:

```bash
# Memory errors and leaks (undefined behavior + AddressSanitizer)
gcc -std=gnu11 -Wall -Wextra -g -O0 -fsanitize=address,undefined -static-libasan \
    -Iinclude -Ithird_party/cjson \
    src/*.c src/tools/*.c third_party/cjson/cJSON.c \
    -lpthread -lcurl -o maskd_asan

# Data races between the reactor thread and LLM worker threads
gcc -std=gnu11 -Wall -Wextra -g -O0 -fsanitize=thread \
    -Iinclude -Ithird_party/cjson \
    src/*.c src/tools/*.c third_party/cjson/cJSON.c \
    -lpthread -lcurl -o maskd_tsan
```

Run either binary in place of `maskd` (e.g. with a small `MASK_RING_CAPACITY`
and short `MASK_TICK_MS` to force ring-buffer wraparound and worker-thread
contention quickly). Use `-static-libasan` — a plain dynamic ASan build can
fail to preload correctly alongside `libcurl` under WSL2 ("ASan runtime does
not come first in initial library list").

## Project layout

```
include/mask/      public headers for every daemon component
src/                daemon implementations (main.c wires it all together)
src/tools/          individual tool implementations
third_party/cjson/  vendored cJSON (MIT)
tests/              standalone smoke tests
bridge/             Node.js WebSocket bridge (daemon TCP/JSON -> browser WS)
web/                Vite + React + Tailwind dashboard
```

## Known limitations

This is a scaffold, not a finished agent:

- **Tool-call parsing is fragile.** It expects the model to reply with bare
  JSON and nothing else. A chatty model that wraps its answer in prose or
  markdown fences will silently fail to trigger a tool call.
- **Two tools only.** `sysinfo` and `run_shell` are examples, not a real
  capability set.
- **No persistent memory.** The daemon's ring buffer is in-process and lost
  on restart. The bridge's history buffer is longer-running but also
  in-memory only — it resets if the bridge process restarts.
- **No capability policy.** Any registered tool can be called by the model
  with no allow/deny rules or human-in-the-loop confirmation beyond the
  sandbox's resource limits. The dashboard can change daemon *config*
  live (tick rate, reasoning cadence, pause, LLM endpoint/model) but still
  cannot trigger a *tool* directly — that only applies to what the LLM
  itself decides to call.
- **`set_config` has no authentication.** Anything that can reach the
  loopback IPC port can change the daemon's live config — fine for a
  single-user local tool, not fine if this ever needs multi-user or
  network-exposed deployment.
- **Single LLM worker at a time.** Reasoning cycles are serialized (a busy
  cycle is skipped rather than queued), which is deliberate for safety but
  means a slow model call delays the next cycle rather than overlapping it.
- **`run_shell` cannot run interactive/multi-arg-per-word commands
  correctly.** The model sometimes splits a sentence across multiple argv
  elements when calling `echo` (e.g. one word per array entry) instead of
  one string — the command still runs, just not exactly as intended. Worth
  tightening the tool-call prompt/schema.

See [CHANGELOG.md](CHANGELOG.md) for what's changed between versions.

## License

MIT — see [LICENSE](LICENSE). The vendored `cJSON` under
[`third_party/cjson/`](third_party/cjson/) is separately MIT-licensed by its
own authors; its license header is preserved in the source file.
