// Bridges maskd's TCP/JSON IPC (one-shot request/response per connection,
// see src/ipc.c) to a browser-friendly WebSocket feed. Polls the daemon on
// an interval and broadcasts each snapshot to every connected client, plus
// a rolling history of sysinfo samples (load average, memory) for charting
// -- the daemon's own ring buffer is short-term and bounded, so
// long-running analytics live here instead of in the C daemon. Also relays
// "set_config"/"get_config" commands from a browser client to the daemon,
// so the dashboard can adjust the daemon's live behavior.
import net from 'node:net';
import { WebSocketServer } from 'ws';

const DAEMON_HOST = process.env.MASK_IPC_HOST || '127.0.0.1';
const DAEMON_PORT = parseInt(process.env.MASK_IPC_PORT || '7717', 10);
const BRIDGE_PORT = parseInt(process.env.BRIDGE_PORT || '8765', 10);
const POLL_INTERVAL_MS = parseInt(process.env.POLL_INTERVAL_MS || '500', 10);
const MAX_HISTORY = 600; // ~5 minutes at a 500ms poll interval

const wss = new WebSocketServer({ port: BRIDGE_PORT });
console.log(`[bridge] WebSocket server listening on ws://localhost:${BRIDGE_PORT}`);
console.log(`[bridge] polling maskd at ${DAEMON_HOST}:${DAEMON_PORT} every ${POLL_INTERVAL_MS}ms`);

/** @type {{ts: number, load1: number, load5: number, load15: number, memUsedPct: number}[]} */
const history = [];
let lastHistoryTs = 0;

/** Sends one JSON command line to the daemon and resolves with its parsed
 * JSON reply. Each call is its own short-lived TCP connection, matching
 * the daemon's one-request-per-connection protocol. */
function sendCommand(command) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: DAEMON_HOST, port: DAEMON_PORT });
    let data = '';
    const timeout = setTimeout(() => {
      socket.destroy();
      reject(new Error('timed out waiting for daemon response'));
    }, 2000);

    socket.on('connect', () => socket.write(JSON.stringify(command) + '\n'));
    socket.on('data', (chunk) => { data += chunk.toString('utf8'); });
    socket.on('end', () => {
      clearTimeout(timeout);
      try {
        resolve(JSON.parse(data));
      } catch (err) {
        reject(new Error(`bad JSON from daemon: ${err.message}`));
      }
    });
    socket.on('error', (err) => {
      clearTimeout(timeout);
      reject(err);
    });
  });
}

function fetchSnapshot() {
  return sendCommand({ cmd: 'snapshot' });
}

function extractSysinfo(entry) {
  try {
    const j = JSON.parse(entry.text);
    if (typeof j.load1 !== 'number' || typeof j.mem_total_kb !== 'number') return null;
    return {
      ts: entry.ts,
      load1: j.load1,
      load5: j.load5,
      load15: j.load15,
      memUsedPct: j.mem_total_kb > 0
        ? ((j.mem_total_kb - j.mem_available_kb) / j.mem_total_kb) * 100
        : 0,
    };
  } catch {
    return null;
  }
}

function updateHistory(entries) {
  for (const e of entries) {
    if (e.role !== 'sysinfo' || e.ts <= lastHistoryTs) continue;
    const point = extractSysinfo(e);
    if (point) {
      history.push(point);
      lastHistoryTs = point.ts;
    }
  }
  while (history.length > MAX_HISTORY) history.shift();
}

function broadcast(message) {
  const payload = JSON.stringify(message);
  for (const client of wss.clients) {
    if (client.readyState === client.OPEN) {
      client.send(payload);
    }
  }
}

let pollTimer = null;

async function pollOnce() {
  try {
    const snapshot = await fetchSnapshot();
    updateHistory(snapshot.entries || []);
    broadcast({ type: 'snapshot', connected: true, ...snapshot, history });
  } catch (err) {
    broadcast({ type: 'snapshot', connected: false, error: err.message, history });
  }
}

function scheduleNextPoll(delayMs = POLL_INTERVAL_MS) {
  clearTimeout(pollTimer);
  pollTimer = setTimeout(pollLoop, delayMs);
}

async function pollLoop() {
  await pollOnce();
  scheduleNextPoll();
}

wss.on('connection', (ws) => {
  console.log('[bridge] client connected');

  ws.on('message', async (raw) => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      ws.send(JSON.stringify({ type: 'error', error: 'malformed message' }));
      return;
    }

    if (msg.type === 'set_config') {
      try {
        const result = await sendCommand({ cmd: 'set_config', config: msg.config });
        ws.send(JSON.stringify({ type: 'set_config_result', ...result }));
        // Re-poll immediately so every client sees the new config right
        // away instead of waiting up to POLL_INTERVAL_MS.
        scheduleNextPoll(0);
      } catch (err) {
        ws.send(JSON.stringify({ type: 'set_config_result', ok: false, error: err.message }));
      }
    } else if (msg.type === 'get_config') {
      try {
        const result = await sendCommand({ cmd: 'get_config' });
        ws.send(JSON.stringify({ type: 'get_config_result', ...result }));
      } catch (err) {
        ws.send(JSON.stringify({ type: 'get_config_result', ok: false, error: err.message }));
      }
    } else {
      ws.send(JSON.stringify({ type: 'error', error: `unknown message type: ${msg.type}` }));
    }
  });

  ws.on('close', () => console.log('[bridge] client disconnected'));
});

pollLoop();
