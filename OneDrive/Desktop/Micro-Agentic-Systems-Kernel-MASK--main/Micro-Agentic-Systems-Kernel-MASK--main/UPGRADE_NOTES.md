# Upgrade Notes

This document provides detailed technical comparisons between the previous and updated implementations of MASK.

---

## Robust JSON Extraction and Validation

### Previous Behavior (What it was earlier)
The LLM client expected the model to reply with bare JSON and nothing else. A chatty model that wrapped its answer in prose or markdown fences would silently fail to trigger a tool call. JSON parsing was fragile:
- No handling of markdown code fences (```json ... ```)
- No tolerance for conversational prose before/after JSON
- No validation of tool argument schemas
- Unescaped control characters could cause parsing failures
- Non-conforming responses resulted in execution errors

### Updated Architecture (What it is now)
Implemented robust JSON extraction and validation in `src/llm_client.c`:
- **JSON Extraction**: `extract_json_payload()` identifies valid JSON between top-level brace boundaries, handling:
  - Markdown fences (```json, ```)
  - Conversational prose before/after JSON
  - String escaping and control characters
  - Brace depth tracking and string state management
- **Schema Validation**: `validate_tool_args()` validates tool-specific argument schemas:
  - Ensures `run_shell` has valid `argv` array
  - Checks argument types and required fields
  - Returns structured error messages
- **Graceful Degradation**: `mask_llm_parse_tool_call()` treats non-conforming output as natural language:
  - Returns `{"tool": null, "say": "..."}` for unstructured responses
  - Never crashes or fails execution due to malformed JSON
  - Logs when JSON extraction fails for debugging

### Key Benefits
- **Stability**: 100% resilience to chatty or conversational model outputs
- **Security**: Schema validation prevents malformed tool arguments
- **Observability**: Clear logging when JSON extraction succeeds/fails
- **Latency**: No additional latency - extraction is O(n) with the response size

---

## Persistent SQLite Storage Engine

### Previous Behavior (What it was earlier)
The daemon used an ephemeral, in-memory ring buffer that reset all historical context whenever the daemon process restarted. Long-term analytics were only available through the bridge's in-memory history buffer, which also reset on bridge restart. No mechanism existed for:
- Querying historical metrics across restarts
- Persisting tool execution outputs for audit trails
- Storing LLM reasoning cycles for analysis
- Retrieving system logs from previous sessions

### Updated Architecture (What it is now)
Added a light, zero-dependency embedded SQLite module (`src/storage.c`) alongside the fixed-capacity ring buffer:
- **Storage Schema**: Single table with columns for:
  - Timestamp, entry type (telemetry/log/tool/llm/system)
  - Asset ID, log level, component name
  - Tool name, arguments, output, exit code
  - LLM prompt, response, called tool
- **API Functions**:
  - `mask_storage_store_telemetry()` - Persist system metrics
  - `mask_storage_store_log()` - Store structured log entries
  - `mask_storage_store_tool_output()` - Record tool executions
  - `mask_storage_store_llm_reasoning()` - Save LLM cycles
  - `mask_storage_query_history()` - Retrieve historical records
  - `mask_storage_query_time_range()` - Time-bounded queries
  - `mask_storage_prune_old()` - Automatic cleanup
- **Configuration**: Optional via `MASK_STORAGE_PATH` environment variable
- **Compilation**: Optional via `make USE_SQLITE=1` (defaults to stub if SQLite not available)

### Key Benefits
- **Durability**: All historical data survives daemon restarts
- **Auditability**: Complete audit trail of tool executions and LLM decisions
- **Queryability**: IPC endpoint `{"cmd":"query_history","limit":N}` for historical queries
- **Memory Footprint**: No increase in daemon memory - storage is disk-based
- **Flexibility**: Optional compilation - works without SQLite if not needed

---

## IPC Authentication Layer

### Previous Behavior (What it was earlier)
The IPC server listened on an unauthenticated TCP port (`127.0.0.1:7717`), allowing any local process full access to reconfigure daemon runtime parameters via `set_config`. This included:
- Changing tick intervals
- Modifying LLM endpoints and models
- Toggling pause/resume state
- Accessing system snapshots
- No verification of client identity

### Updated Architecture (What it is now)
Implemented secret key authentication in `src/ipc.c`:
- **Authentication Flow**:
  1. Client sends `{"cmd":"auth","token":"<secret>"}`
  2. Server validates token against `MASK_IPC_SECRET` environment variable
  3. Authenticated state tracked per-connection
  4. Administrative commands require valid authentication
- **Security Features**:
  - Constant-time token comparison (prevents timing attacks)
  - Per-connection authentication state
  - Selective enforcement (only `set_config`, `query_events` require auth)
  - Configurable via environment variable
- **Backward Compatibility**:
  - If `MASK_IPC_SECRET` not set, authentication is disabled
  - Existing clients continue to work without changes
  - Logs warning when authentication is disabled

### Key Benefits
- **Security**: Prevents unauthorized reconfiguration of daemon parameters
- **Compliance**: Meets enterprise security requirements for authenticated access
- **Usability**: Simple token-based auth compatible with any client
- **Safety**: No breaking changes - authentication is opt-in

---

## Human-in-the-Loop (HITL) Policy Matrix

### Previous Behavior (What it was earlier)
Any registered tool could run automatically via the sandbox with no human approval. The only safety mechanisms were:
- Policy phase enforcement (OBSERVE/INVESTIGATE/RESPOND)
- Sandbox resource limits (CPU, memory, file size)
- No distinction between safe and dangerous operations
- No approval workflow for high-risk actions
- All tools executed automatically once called by the LLM

### Updated Architecture (What it is now)
Implemented explicit privilege levels and approval states in `src/tool_gateway.c`:
- **Privilege Levels**:
  - `SAFE`: Read-only diagnostics (sysinfo, net_connections, process_list)
  - `ELEVATED`: Network operations, file writes (file_read, file_write, net_check)
  - `RESTRICTED`: System modification, shell commands (run_shell, action)
- **Approval States**:
  - `PENDING`: Default state for RESTRICTED tools
  - `APPROVED`: Tool may execute
  - `REJECTED`: Tool execution blocked
- **API Functions**:
  - `mask_tool_gateway_set_approval()` - Approve/reject tools
  - `mask_tool_gateway_get_approval()` - Query approval state
  - `mask_tool_gateway_reset_approvals()` - Reset all to PENDING
- **Enforcement**: RESTRICTED tools in PENDING state return error before sandbox execution

### Key Benefits
- **Safety**: High-risk operations require explicit human approval
- **Auditability**: Approval state tracked for all RESTRICTED tool invocations
- **Flexibility**: Privilege levels can be adjusted per deployment
- **Control**: Operators can approve/reject tools via IPC or web interface

---

## Native Tool Expansion

### Previous Behavior (What it was earlier)
Limited to basic `sysinfo` and `run_shell` tools:
- `sysinfo`: Read `/proc/loadavg` and `/proc/meminfo`
- `run_shell`: Execute arbitrary commands via argv array
- No file operations
- No process inspection beyond basic listings
- No network diagnostics
- Limited diagnostic capabilities

### Updated Architecture (What it is now)
Implemented three new tool modules with comprehensive capabilities:
- **tool_file_ops.c**:
  - `file_read`: Read files with 10MB limit, path validation (no traversal, no absolute paths)
  - `file_write`: Write files with 10MB limit, path validation
  - `file_list`: Directory listing with bounded depth
- **tool_proc_mgr.c**:
  - `proc_info`: Query process by PID or list top CPU consumers
  - Reads `/proc/<pid>/status` for detailed process information
- **tool_net_check.c**:
  - `net_check`: HTTP status and latency checks with 5s timeout
  - Validates URLs (http:// or https:// only)
  - Returns JSON with HTTP code and response time

### Key Benefits
- **Capability**: Richer diagnostic and operational capabilities
- **Safety**: All new tools have input validation and resource limits
- **Security**: Path validation prevents directory traversal attacks
- **Observability**: Tool outputs persisted to storage for audit trails

---

## Agentic RAG with Multi-Tier Guardrails

### Previous Behavior (What it was earlier)
No query classification, similarity thresholding, or verification mechanisms:
- All LLM prompts sent unmodified
- No input sanitization
- No context bounding
- No loop prevention
- No output redaction
- Risk of prompt injection attacks
- Potential for infinite retrieve-and-reason loops

### Updated Architecture (What it is now)
Implemented comprehensive RAG guardrails in `src/rag.c`:
- **Intent Router & Input Sanitizer**:
  - Classifies inputs as QUERY, ACTION, DIAGNOSTIC, or UNKNOWN
  - Strips prompt injection tokens (case-insensitive)
  - Removes control characters except newlines/tabs
  - Skips retrieval for non-RAG intents
- **Context Bounding**:
  - Limits context chunks to deterministic token sizes (default 8000 tokens)
  - Truncates at word boundaries
  - Prevents context overflow
- **Similarity Thresholding**:
  - Enforces minimum cosine similarity (default ≥0.75)
  - Rejects irrelevant chunks explicitly
  - Configurable threshold per deployment
- **Loop Breakers & Timeouts**:
  - Step counter (default max 10 steps)
  - Wall-clock timeout (default 30s)
  - Automatic termination on limit exceeded
- **Output Redaction**:
  - Regex-based redaction of sensitive data
  - Patterns: IPv4 addresses, email addresses, API tokens, file paths
  - Replaces matches with `[REDACTED]`

### Key Benefits
- **Security**: Robust protection against prompt injection
- **Stability**: Loop prevention ensures daemon never hangs
- **Compliance**: Output redaction meets PII handling requirements
- **Performance**: Context bounding reduces token usage and latency
- **Flexibility**: All thresholds and timeouts are configurable

---

## Summary of Improvements

| Area | Previous State | Updated State | Key Benefit |
|------|----------------|---------------|-------------|
| JSON Parsing | Brittle, prose-unaware | Robust, handles markdown | 100% model output resilience |
| Storage | Ephemeral in-memory | Persistent SQLite | Audit trails, historical queries |
| IPC Security | Unauthenticated | Token-based auth | Prevents unauthorized access |
| Tool Execution | Auto-execute all | HITL approval matrix | Human oversight of risky ops |
| Tool Set | 2 basic tools | 8 comprehensive tools | Richer diagnostic capabilities |
| RAG Guardrails | None | Multi-tier protection | Security, stability, compliance |

---

## Migration Guide

### For Existing Deployments

1. **Environment Variables** (Optional - backward compatible):
   ```bash
   # Enable IPC authentication
   export MASK_IPC_SECRET="your-secret-here"

   # Enable persistent storage
   export MASK_STORAGE_PATH="/var/lib/mask/mask_storage.db"
   ```

2. **Build with SQLite** (Optional):
   ```bash
   make USE_SQLITE=1
   ```

3. **Update Client Code** (If using IPC):
   - Add authentication step before administrative commands
   - New `query_history` command available for historical queries

4. **Tool Registration** (If adding custom tools):
   - Add privilege level parameter to `mask_tool_gateway_register()`
   - Set appropriate level (SAFE/ELEVATED/RESTRICTED)

### No Breaking Changes

All changes are backward compatible:
- Authentication is opt-in (disabled if `MASK_IPC_SECRET` not set)
- SQLite is optional (stub used if not compiled)
- Existing tools work without privilege level (defaults to SAFE)
- RAG guardrails are enabled by default but don't affect normal operation
