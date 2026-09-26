# MASK × WOLF-PAK Roadmap Correlation

## The road map (as given)

```
┌────────────────────────┐
│    WOLF-PAK PLATFORM   │
└────────────┬───────────┘
             │
┌────────────┼────────────────────┐
│            │                    │
▼            ▼                    ▼
NETWORK      PAKSHIELD        MASK NETWORK
GUARDIAN     Identity Risk    Network/Asset
Network Sec  & Access         Intelligence
Detection/   │                    │
Response     └────────────────────┼────────────────────┘
             │                    │
             ▼                    ▼
┌────────────────────────┐
│ WOLF-PAK SECURITY CORE│
├────────────────────────┤
│ Event Fabric          │
│ Security Graph        │
│ Threat Intel          │
└──────────┬─────────────┘
           ▼
┌────────────────────────┐
│ AI SECURITY ORCHESTRATOR│
├────────────────────────┤
│ Detection              │
│ Investigation          │
│ Response               │
└──────────┬─────────────┘
           ▼
┌────────────────────────┐
│ POLICY ENGINE          │
└──────────┬─────────────┘
           ▼
┌────────────────────────┐
│ ACTION GATEWAY         │
└──────────┬─────────────┘
           ▼
┌────────────────────────┐
│ CUSTOMER ENVIRONMENT   │
└────────────────────────┘
```

---

## 1. Where MASK sits today

MASK is currently a **single-host system-monitoring daemon** with an LLM reasoning loop. Its actual surface area (as of 2026-09-27):

| Layer | What MASK has now | What the road map expects |
|---|---|---|
| **Top app** | Host metrics (load, memory) + network telemetry (`ss`/`netstat`) + asset inventory (process list, user sessions) + structured response actions + security-aware LLM reasoning loop | Network/Asset Intelligence — discovery, telemetry, asset inventory ✓ |
| **Security Core** | Structured event envelope on every tool output + per-host JSONL event log + asset ID + IOC data + policy phase in prompt + ring buffer | Event Fabric (ingestion pipeline) — event envelope + log ✓; Security Graph (host node with process/network/user state) — tools feed it ◑; Threat Intel (IOC block in prompt) — static IOC ◑ |
| **AI Orchestrator** | LLM cycle with security-framed prompt ("Network/Asset Intelligence agent of the WOLF-PAK security platform"), IOC context, policy phase awareness, tool manifest with phase labels | Detection (security observations + prompt) ✓; Investigation (tool execution + ring buffer trail) ✓; Response (action tool gated by phase) ✓ |
| **Policy/Action** | Tool gateway enforces min_phase per tool (OBSERVE < INVESTIGATE < RESPOND); run_shell requires INVESTIGATE; action tool requires RESPOND; 3 structured actions (kill_process, disable_user, isolate_ip) | Policy Engine — phase gating ✓ (seed); Action Gateway — structured actions ✓ (seed), no human-in-the-loop gating yet |

**Legend:** ✓ = implemented in code; ◑ = partially implemented / data available but no consuming consumer yet; ✗ = not started.

MASK is the leftmost app in the top row: **MASK NETWORK**. The road map says it provides *Network/Asset Intelligence*. The code now structurally fills that role — network telemetry tools, asset inventory tools, event envelopes, asset ID, IOC data in prompt, policy phase gating, and structured response actions are all in place.

---

## 2. How the three top apps correlate

### 2.1 Network Guardian ↔ MASK

**Network Guardian owns:** network-layer detection and response. Packet analysis, flow monitoring, IDS/IPS signals, network anomaly detection.

**MASK owns (today):** host-layer observation. Load, memory, process execution, arbitrary command output.

**Correlation points:**

- MASK is the **host sensor** that Network Guardian can query. When Network Guardian sees a suspicious network event targeting host X, it can ask MASK to run `sysinfo`, inspect processes, read logs, or execute a diagnostic command on host X via the sandbox.
- MASK's LLM reasoning cycle can run *host-based detection* that complements Network Guardian's *network-based detection*. Example: MASK notices a spike in load average + unusual process list → surfaces a host anomaly that Network Guardian correlates with a network flow.
- The **Event Fabric** is the shared bus. MASK pushes host events in; Network Guardian pushes network events in; the Security Graph correlates them.
- MASK's `run_shell` tool, if hardened into an allowlist, becomes the **host execution arm** that Network Guardian's response playbooks can invoke — "isolate host", "capture memory dump", "kill process by PID".

**What MASK needs from this correlation:** a structured output format (not just raw JSON blobs) that the Event Fabric can ingest. Right now `sysinfo` emits `{"load1":..., "mem_total_kb":...}` — that's fine for a chart, not fine for a security event pipeline. Needs severity, category, asset ID, timestamp in a standard envelope.

**What MASK provides now:** every tool output (sysinfo + all LLM-dispatched tool calls) is wrapped in `mask_event_envelope()` producing `{"timestamp_ms","asset_id","source":"MASK","event_type","severity","payload":{...}}`. The per-host JSONL event log (`MASK_EVENT_LOG_PATH`) writes these as structured JSON Lines. IPC `get_config` returns the full config including `asset_id`, `ioc_data`, `policy_phase`, and `event_log_path`. Network Guardian can read MASK's event log, query the IPC snapshot, or poll the ring buffer to get structured host events that correlate with its network-flow events by `asset_id`.

### 2.2 PAKSHIELD ↔ MASK

**PAKSHIELD owns:** identity risk scoring, access control decisions, credential/identity anomaly detection.

**MASK owns (today):** nothing identity-related. It doesn't know who's logged in, what sessions exist, or what access patterns look like.

**Correlation points:**

- MASK can be PAKSHIELD's **host-context supplier**. When PAKSHIELD evaluates a risky login, it can query MASK for: active users on the host, recent sudo/su activity, running processes under that user, open network connections owned by that user. MASK's `run_shell` can execute `who`, `last`, `ss`, `ps` — all the host-identity telemetry PAKSHIELD needs.
- MASK's ring buffer stores a timeline of host events. PAKSHIELD can correlate an identity event (e.g. "admin logged in at 03:00") with MASK's host timeline (e.g. "at 03:00 load spiked, unusual process started") to produce a richer risk score.
- The **Security Graph** connects identities (PAKSHIELD) to hosts (MASK) to network flows (Network Guardian). MASK provides the host node and its observations; PAKSHIELD provides the identity node and its risk signals.

**What MASK needs from this correlation:** tools that surface identity/access data. Currently `sysinfo` and `run_shell` are generic — `run_shell` can run `who` but there's no structured identity tool. A `host_users`, `active_sessions`, or `process_by_user` tool would directly serve PAKSHIELD's needs.

**What MASK provides now:** `user_sessions` (logged-in users, TTY, login time, remote host) and `process_list` (all processes with PID, user, CPU%, mem, command) are structured tools available in OBSERVE phase. `net_connections` includes owning PID and process name, so PAKSHIELD can correlate a user → their processes → their network connections. All three tools emit structured JSON wrapped in the standard event envelope with the host's `asset_id`.

### 2.3 MASK as the "Network/Asset Intelligence" app

The road map labels MASK as **Network/Asset Intelligence**. As of 2026-09-27, MASK implements both halves:

**Asset Intelligence (implemented):**
- `sysinfo` — CPU load (1/5/15 min), memory total/available in KB
- `process_list` — all processes: PID, user, stat, CPU%, mem%, VSZ, RSS, comm, optional cmdline
- `user_sessions` — logged-in users: user, TTY, login time, remote host

**Network Intelligence (implemented):**
- `net_connections` — active TCP/UDP connections: proto, state, local address, peer address, owning PID, process name. Filters by proto (tcp/udp).
- `listening_sockets` — TCP/UDP sockets in LISTEN state: proto, local address, peer (0.0.0.0:*), owning PID, process name.

**Asset ID (implemented):** `MASK_ASSET_ID` env var or `set_config` IPC — stable host identity, max 64 chars, used in every event envelope and the LLM prompt.

The **asset inventory** that MASK maintains becomes a node in the Security Graph. Network Guardian adds the network-edges; PAKSHIELD adds the identity-edges. MASK's job is to keep its host node accurate (via the tools above) and feed observations into the Event Fabric (via the event envelope + JSONL log).

---

## 3. Mapping MASK's internals to the lower layers

### 3.1 Event Fabric — IMPLEMENTED (seed)

Every tool output (sysinfo tick + LLM-dispatched tool calls) is wrapped in `mask_event_envelope()`:

```json
{"timestamp_ms":"...","asset_id":"...","source":"MASK","event_type":"...","severity":"info","payload":{...}}
```

The per-host event log (`MASK_EVENT_LOG_PATH` / `set_config event_log_path`) writes these as JSONL. The IPC `get_config` endpoint returns the full config including `asset_id`, `ioc_data`, `policy_phase`, and `event_log_path`.

**What's left:** No network consumer yet. The event log is a file on disk; the next step is an HTTP POST or a Unix socket forwarder to a real Event Fabric intake endpoint.

### 3.2 Security Graph — DATA AVAILABLE, NO CONSUMER

MASK's tools produce the host node's properties:
- Processes: `process_list` → {pid, user, stat, cpu, mem, vsz, rss, comm, cmdline}
- Network: `net_connections` + `listening_sockets` → {proto, state, local, peer, pid, process}
- Users: `user_sessions` → {user, tty, login, host}

These are the host node's edges in the Security Graph. The graph itself (asset relationships, cross-host correlations) lives in a separate component that reads MASK's events.

### 3.3 Threat Intel — STATIC IOC IN PROMPT

`MASK_IOC_DATA` env var or `set_config ioc_data` injects a free-form IOC block into the LLM prompt:

```
Threat intelligence indicators (IOCs) — treat any observation matching these as high severity: <ioc_data>
```

The LLM evaluates observations against these IOCs during its reasoning cycle. No automated feed polling yet — the IOC data is set manually or by an external config manager.

### 3.4 AI Security Orchestrator — IMPLEMENTED (detection + investigation + response)

**Detection:** The LLM prompt frames MASK as "the Network/Asset Intelligence agent of the WOLF-PAK security platform." It receives recent ring buffer observations (sysinfo, previous tool outputs, LLM commentary) and evaluates them. The sysinfo tick runs every tick; the LLM cycle runs every N ticks.

**Investigation:** The tool gateway provides `run_shell` (investigate phase), `net_connections`, `listening_sockets`, `process_list`, `user_sessions` (all observe phase). The LLM can call these to investigate. The ring buffer holds the investigation trail.

**Response:** The `action` tool (respond phase only) provides structured response actions:
- `kill_process` — SIGTERM/SIGKILL by PID
- `disable_user` — lock account via nologin
- `isolate_ip` — iptables DROP rule by IP

The policy phase gate ensures these can only be called when the daemon is in RESPOND phase.

### 3.5 Policy Engine — IMPLEMENTED (seed: phase gating)

The tool gateway enforces a minimum policy phase per tool:

| Phase | Value | Tools available |
|---|---|---|
| OBSERVE | 0 | sysinfo, net_connections, listening_sockets, process_list, user_sessions |
| INVESTIGATE | 1 | + run_shell |
| RESPOND | 2 | + action (kill_process, disable_user, isolate_ip) |

Set via `MASK_POLICY_PHASE` env var or `set_config policy_phase` ("observe", "investigate", "respond"). The LLM job snapshots the phase at spawn time; dispatch enforces the tool's min_phase ≤ current phase.

**What's left:** No per-tool argument validation, no asset scoping, no dynamic allow/deny rules beyond the phase gate. The phase gate is the seed; a full policy engine would add argument allowlists, asset-based routing, and temporal constraints.

### 3.6 Action Gateway — IMPLEMENTED (seed: structured actions)

The `action` tool accepts structured descriptors:

```json
{"action": "kill_process", "pid": 1234, "signal": "SIGTERM"}
{"action": "disable_user", "username": "alice"}
{"action": "isolate_ip", "ip": "192.168.1.100"}
```

Each action validates its input and executes through the existing sandbox (fork+execvp+setrlimit+poll timeout). The command surface is a known set of 3 actions, not arbitrary argv.

**What's left:** No audit log beyond the event log, no human-in-the-loop gating, no response playbooks (the LLM picks one action per cycle, not a sequenced playbook).

---

## 4. Gap status summary (as of 2026-09-27)

### Layer 0 — event schema

| # | Gap | Status |
|---|---|---|
| 0.1 | No structured event envelope. `sysinfo` emits a bare JSON object with no timestamp in the payload (timestamp is only in the ring buffer metadata), no severity, no category, no asset ID. | ✓ **CLOSED** — `mask_event_envelope()` wraps every tool output; event log writes JSONL |

### Layer 1 (asset intelligence — makes MASK a real "Network/Asset Intelligence" app)

| # | Gap | Status |
|---|---|---|
| 1.1 | No network telemetry tools. Zero coverage of active connections, listening sockets, DNS, bandwidth. | ✓ **CLOSED** — `net_connections` + `listening_sockets` via `ss` |
| 1.2 | No asset inventory tools beyond `sysinfo`. No process list, no disk mounts, no user list, no port scan. | ✓ **CLOSED** — `process_list` via `ps` + `user_sessions` via `who` |
| 1.3 | Tools return ad-hoc JSON with no schema. `sysinfo` returns `{"load1":..., "mem_total_kb":...}`. `run_shell` returns raw command output. No consistent envelope. | ✓ **CLOSED** — all tool output wrapped in standard event envelope |

### Layer 2 (orchestration — makes the LLM loop security-aware)

| # | Gap | Status |
|---|---|---|
| 2.1 | The system prompt asks the LLM to monitor system health, not security. "You are the reasoning core of MASK, a system-monitoring daemon." | ✓ **CLOSED** — prompt says "Network/Asset Intelligence agent of the WOLF-PAK security platform" |
| 2.2 | No threat intel in the prompt. The LLM has no IOC list, no CVE context, no attack-pattern knowledge to evaluate observations against. | ✓ **CLOSED** — IOC data block injected into prompt from `MASK_IOC_DATA` / `set_config ioc_data` |
| 2.3 | Investigation is unconstrained. `run_shell` can run any command, so the LLM can investigate anything — but also can't be trusted to investigate safely in a production setting. | ◑ **PARTIAL** — `run_shell` still accepts any argv; phase-gated to INVESTIGATE but no command allowlist |

### Layer 3 (policy and action — makes response safe and governable)

| # | Gap | Status |
|---|---|---|
| 3.1 | No capability policy. Any registered tool can be called by the LLM with no allow/deny rules. | ◑ **PARTIAL** — phase gating (OBSERVE/INVESTIGATE/RESPOND) enforced in tool gateway; no argument-level allowlist |
| 3.2 | `run_shell` is a general command executor, not an action gateway. Response actions are just commands. | ◑ **PARTIAL** — `action` tool provides 3 structured response actions (kill_process, disable_user, isolate_ip); `run_shell` still general but gated to INVESTIGATE phase |
| 3.3 | No authentication on IPC `set_config`. Anything that can reach the loopback port can change the daemon's config, including the LLM endpoint and model. | ✗ **OPEN** — localhost-only, no auth; acceptable for single-host deployment |

### Layer 4 (cross-app integration — makes the three apps actually correlate)

| # | Gap | Status |
|---|---|---|
| 4.1 | MASK has no integration contract with Network Guardian or PAKSHIELD. No API, no protocol, no shared event format. | ✗ **OPEN** — no API/protocol; event log + IPC snapshot are the primitives an integration would build on |
| 4.2 | No asset ID scheme. MASK doesn't identify its host with a stable ID that the Security Graph can use as a node key. | ✓ **CLOSED** — `MASK_ASSET_ID` config field, max 64 chars |
| 4.3 | No outbound event export. MASK stores events in a ring buffer but never sends them anywhere. | ◑ **PARTIAL** — JSONL event log file exists; no network export (HTTP/socket forwarder) yet |

---

## 5. What MASK already does well that the road map needs

These are assets, not gaps:

- **Bounded memory:** The ring buffer's fixed capacity and overwrite semantics are exactly what an event pipeline needs for a host sensor — it can't be used to exhaust memory by flooding it with events.
- **Sandboxed execution:** The `fork`+`execvp`+`setrlimit`+`poll` timeout model is the right foundation for a response action gateway. It already isolates the effect of a tool call.
- **Non-blocking reactor:** The LLM cycle runs on a detached worker thread; the reactor never blocks on network I/O. This matters when MASK is pulling threat intel feeds or pushing events to the Event Fabric — those are network operations and shouldn't stall the detection loop.
- **Live config:** The IPC `set_config` mechanism proves the daemon can be reconfigured at runtime without restart. That pattern extends to policy config, threat intel feed URLs, and asset ID assignment.
- **Tool manifest:** The tool gateway's manifest generation is a clean way to tell the LLM what's available. Adding new tools (network telemetry, identity, asset inventory) automatically extends the LLM's capability surface.
- **Web dashboard:** The bridge + React dashboard is a working observability plane. For the road map, this becomes the **investigation UI** — an analyst can watch MASK's reasoning cycles, see what tools it called, and review the event log.

---

## 6. What's done and what's left (as of 2026-09-27)

The shortest path from the original gap analysis had 7 steps. Here's where each stands:

| # | Step | Status |
|---|---|---|
| 1 | Give MASK an asset ID | ✓ Done — `MASK_ASSET_ID` env var / `set_config` IPC, max 64 chars |
| 2 | Wrap every tool output in a standard event envelope | ✓ Done — `mask_event_envelope()` + JSONL event log |
| 3 | Add two network-telemetry tools | ✓ Done — `net_connections` + `listening_sockets` via `ss` |
| 4 | Add two asset-inventory tools | ✓ Done — `process_list` via `ps` + `user_sessions` via `who` |
| 5 | Change the system prompt to security-reasoning | ✓ Done — prompt says "Network/Asset Intelligence agent of the WOLF-PAK security platform" |
| 6 | Add a capability policy filter | ◑ Partial — phase gating (OBSERVE/INVESTIGATE/RESPOND) in tool gateway; no argument allowlist yet |
| 7 | Export events somewhere | ◑ Partial — JSONL event log file; no network export (HTTP/socket forwarder) yet |

Steps 1–5 are complete. Step 6 has the phase gate (the seed of the Policy Engine). Step 7 has the file-based event log (the seed of the Event Fabric export). The remaining work is in the section below.

## 7. Remaining work to fully occupy the Network/Asset Intelligence slot

### Must have (before MASK can claim the role end-to-end)

1. **Outbound event export.** The JSONL event log is a file on disk. A network consumer (Event Fabric) needs events pushed — either an HTTP POST to an intake endpoint, or a Unix socket that a forwarder reads. The `mask_event_log_write` primitive is the hook; the export transport is the missing piece.

2. **Command allowlist for run_shell.** The investigation gap (2.3) is that `run_shell` accepts any argv. A simple allowlist (e.g. `who`, `last`, `ss`, `ps`, `journalctl`, `grep`) would make investigation safe without losing flexibility. This is the next policy engine increment after the phase gate.

3. **Threat intel feed.** The IOC block in the prompt is static. A feed poller (curl a URL, parse the response, update `ioc_data` via `set_config`) would make detection actually find threats instead of relying on manually updated IOCs.

### Nice to have (after the above prove the pattern)

4. **Human-in-the-loop gating on the Action Gateway.** Before executing a RESPOND-phase action, require an explicit approve signal (IPC `approve_action` command, or a flag in the event log that an external operator clears).

5. **Response playbooks.** Instead of the LLM picking one action per cycle, a playbook executor runs a sequenced list of actions (isolate → kill → notify) with per-step success/failure checks.

6. **Bidirectional integration with Network Guardian and PAKSHIELD.** A shared event protocol (e.g. both apps emit JSONL with `asset_id`, `source`, `event_type`, `severity`, `payload`) and a correlation query interface (IPC `query_events` that filters by asset_id + time range + event_type).

---

## 8. Verification

Build: `wsl -e bash -c 'cd /mnt/c/Users/CodyC/MASK && make clean && make'`
Tests: `wsl -e bash -c 'cd /mnt/c/Users/CodyC/MASK && make test'`
End-to-end: `MASK_ASSET_ID=host-w2026 MASK_EVENT_LOG_PATH=/tmp/mask_events.jsonl ./maskd &` — verify JSONL events appear with `asset_id`, `source: "MASK"`, `event_type`, `severity`, and typed `payload`.
