import socket, json, subprocess, time, os, signal

# Start mask
env = os.environ.copy()
env["MASK_ASSET_ID"] = "dbgtest"
env["MASK_TICK_MS"] = "2000"
env["MASK_EVENT_LOG_PATH"] = "/tmp/ipc_debug.jsonl"

proc = subprocess.Popen(
    ["/mnt/c/Users/CodyC/MASK/maskd"],
    env=env,
    stdout=open("/tmp/mask_dbg.log", "w"),
    stderr=subprocess.STDOUT,
)

time.sleep(2)

# Connect and send query
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(5)
s.connect(("127.0.0.1", 7717))
req = json.dumps({"cmd": "query_events", "filter": {"asset_id": "dbgtest"}}) + "\n"
print("SENT:", repr(req))
s.sendall(req.encode())
buf = b""
while b"\n" not in buf:
    chunk = s.recv(4096)
    if not chunk:
        break
    buf += chunk
s.close()
print("RCVD RAW:", repr(buf))
try:
    print("RCVD PARSED:", json.loads(buf.split(b"\n", 1)[0]))
except Exception as e:
    print("PARSE ERROR:", e)

proc.send_signal(signal.SIGTERM)
proc.wait(timeout=5)
print("MASK exited cleanly")
