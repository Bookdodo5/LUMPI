#!/usr/bin/env python3
"""
LUMPI Teacher Dashboard Server

One-file Python server using only standard library.
Receives telemetry from nest, serves dashboard page and JSON APIs.
"""

import os
import sys
import json
import hmac
import time
import socket
import signal
from http.server import HTTPServer, BaseHTTPRequestHandler
from collections import deque
from urllib.parse import urlparse, parse_qs

# ============================================================
# CONSTANTS (top of file, easy to change)
# ============================================================
PORT = 8202
MAX_BODY_BYTES = 2048
STALE_AFTER_S = 30        # 3 missed 10 s heartbeats
OFFLINE_AFTER_S = 60
RECENT_SIZE = 20          # recent events table
ERROR_SIZE = 50           # rejected messages kept
SEEN_LIMIT = 1000         # event IDs remembered for duplicate check
VALID_CLOCK_AFTER = 1700000000
HISTORY_SIZE = 720        # one hour at one message per 5 s, per node
HISTORY_MINUTES = [5, 15, 60]
DARK_LUX = 100.0          # same as dark_lux in config/settings.h
SHOWN_NODES = [0, 1, 2]   # always listed, even before any data
VALID_NODES = [0, 1, 2, 3]

# Required data keys per event type
REQUIRED_DATA = {
    'room': ['lux', 'distance_cm'],
    'start': ['uid', 'passage', 'attempt', 'fsr'],
    'status': ['uid', 'passage', 'attempt', 'fsr']
}

# Valid activities
ACTIVITIES = ['idle', 'book_on', 'recording', 'waiting_ai', 'feedback', 'quiet_study', 'stopped', None]

# Example correct packets (for error hints)
EXAMPLES = {
    'room': {"event_id": "lumpi_1a2b3c4d/42", "node_id": 0, "event_type": "room", "recorded_at": 1794794400, "activity": None, "data": {"lux": 230.5, "distance_cm": 160.0}},
    'start': {"event_id": "lumpi_1a2b3c4d/43", "node_id": 1, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "fsr": 3100}},
    'status': {"event_id": "lumpi_1a2b3c4d/44", "node_id": 1, "event_type": "status", "recorded_at": 1794794411, "activity": "recording", "data": {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "fsr": 3100}}
}

# Hints for error messages: (substring to match, hint text)
HINTS = [
    ("event is not a JSON object", "Send one JSON object {...}, not a list or a single value."),
    ("missing event_id", "Include \"event_id\": \"gateway_id/number\" (string with a slash)."),
    ("missing node_id", "Include \"node_id\": 0 for nest, 1–3 for dolls (number, not string)."),
    ("missing event_type", "Include \"event_type\": \"room\" (nest) or \"start\"/\"status\" (dolls)."),
    ("missing recorded_at", "Include \"recorded_at\": unix seconds (number, > 1700000000)."),
    ("missing activity", "Include \"activity\": one of idle, book_on, recording, waiting_ai, feedback, quiet_study, stopped, or null."),
    ("missing data", "Include \"data\": { ... } object with required keys."),
    ("bad event_id", "event_id must be a string containing \"/\" and shorter than 96 chars."),
    ("bad node_id", "node_id must be 0, 1, 2, or 3 (number, not boolean or string)."),
    ("bad event_type", "event_type must be \"room\", \"start\", or \"status\"."),
    ("room event must come from node 0", "Set NODE_ID = 0 in nest firmware for room events."),
    ("doll event cannot use node 0", "Dolls must use node_id 1, 2, or 3."),
    ("bad recorded_at", "recorded_at must be unix seconds (number > 1700000000), not ISO string."),
    ("bad activity", "activity must be one of: idle, book_on, recording, waiting_ai, feedback, quiet_study, stopped, or null."),
    ("data is not an object", "data must be a JSON object { ... }, not a list or primitive."),
    ("missing data.", "Add the missing key to data object (see example)."),
    ("bad data.", "Value must be number, string, or null (no boolean, list, object)."),
    ("bad token", "Include header X-LUMPI-Token with the correct LAN key."),
    ("bad length", "Content-Length must be present and <= 2048 bytes."),
    ("not JSON", "Body must be valid JSON.")
]

# Static files to serve
PAGE_FILES = {
    '/': ('dashboard/index.html', 'text/html; charset=utf-8'),
    '/style.css': ('dashboard/style.css', 'text/css; charset=utf-8'),
    '/app.js': ('dashboard/app.js', 'application/javascript; charset=utf-8'),
    '/charts.js': ('dashboard/charts.js', 'application/javascript; charset=utf-8')
}

# ============================================================
# IN-MEMORY STATE
# ============================================================
latest = {}                         # node_id -> {received_at, recorded_at, event_type, activity, values{}}
recent = deque(maxlen=RECENT_SIZE)  # newest first, each event + "received_at"
seen_ids = set()                    # duplicate check
history = {}                        # node_id -> deque(maxlen=HISTORY_SIZE) of {t, recorded_at, event_type, activity, data}
counts = {}                         # node_id -> {"added": n, "duplicate": n}
errors = deque(maxlen=ERROR_SIZE)   # newest first
totals = {"rejected": 0, "error_id": 0}

# ============================================================
# HELPER FUNCTIONS
# ============================================================
def log_message(msg):
    """Print log line (silenced for normal requests)."""
    print(msg, flush=True)

def get_hint(reason):
    """Find first matching hint for an error reason."""
    for match, hint in HINTS:
        if match in reason:
            return hint
    return "Check the event format against the examples."

def node_status(age_s):
    """Compute status from age in seconds."""
    if age_s is None:
        return "unknown"
    if age_s < STALE_AFTER_S:
        return "online"
    if age_s < OFFLINE_AFTER_S:
        return "stale"
    return "offline"

def format_node(node_id, now):
    """Format a node for /api/dashboard/nodes."""
    counts_data = counts.get(node_id, {"added": 0, "duplicate": 0})
    node_data = latest.get(node_id)

    if node_data is None:
        return {
            "node_id": node_id,
            "status": "unknown",
            "counts": counts_data
        }

    age = now - node_data["received_at"]
    status = node_status(age)

    values = node_data.get("values", {})
    low_light = False
    if node_id == 0 and values.get("lux") is not None:
        low_light = values["lux"] < DARK_LUX

    result = {
        "node_id": node_id,
        "status": status,
        "age_s": round(age, 1),
        "last_seen": node_data["received_at"],
        "recorded_at": node_data.get("recorded_at"),
        "event_type": node_data.get("event_type"),
        "activity": node_data.get("activity"),
        "values": values,
        "counts": counts_data
    }
    if node_id == 0:
        result["low_light"] = low_light
    return result

def check_event(ev):
    """Validate event, return first error string or None."""
    # 1. Must be a dict
    if not isinstance(ev, dict):
        return "event is not a JSON object"

    # 2. Required keys present
    required_keys = ["event_id", "node_id", "event_type", "recorded_at", "activity", "data"]
    for key in required_keys:
        if key not in ev:
            return f"missing {key}"

    # 3. event_id
    eid = ev["event_id"]
    if not isinstance(eid, str) or "/" not in eid or len(eid) >= 96:
        return f"bad event_id: got {json.dumps(eid)[:60]}"

    # 4. node_id
    nid = ev["node_id"]
    if not isinstance(nid, int) or isinstance(nid, bool) or nid not in VALID_NODES:
        return f"bad node_id: got {json.dumps(nid)[:60]}"

    # 5. event_type
    etype = ev["event_type"]
    if etype not in ["room", "start", "status"]:
        return f"bad event_type: got {json.dumps(etype)[:60]}"

    # 6. room only from node 0
    if etype == "room" and nid != 0:
        return f"room event must come from node 0: got node {nid}"

    # 7. doll events not from node 0
    if etype in ["start", "status"] and nid == 0:
        return f"doll event cannot use node 0: got {etype}"

    # 8. recorded_at
    rat = ev["recorded_at"]
    if not isinstance(rat, (int, float)) or isinstance(rat, bool) or rat <= VALID_CLOCK_AFTER:
        return f"bad recorded_at: got {json.dumps(rat)[:60]}"

    # 9. activity
    act = ev["activity"]
    if act not in ACTIVITIES:
        return f"bad activity: got {json.dumps(act)[:60]}"

    # 10. data is object
    data = ev["data"]
    if not isinstance(data, dict):
        return f"data is not an object: got {json.dumps(data)[:60]}"

    # 11. required data keys
    required = REQUIRED_DATA.get(etype, [])
    for key in required:
        if key not in data:
            return f"missing data.{key}"

    # 12. values plain (number, string, null)
    for key, val in data.items():
        if not (isinstance(val, (int, float, str)) or val is None) or isinstance(val, bool):
            return f"bad data.{key}: got {json.dumps(val)[:60]}"

    return None

def add_event(ev, now, source_ip):
    """Add a valid event to state. Returns 'added' or 'duplicate'."""
    eid = ev["event_id"]
    nid = ev["node_id"]

    # Duplicate check
    if eid in seen_ids:
        counts.setdefault(nid, {"added": 0, "duplicate": 0})
        counts[nid]["duplicate"] += 1
        return "duplicate"

    # Remember ID (clear at limit)
    seen_ids.add(eid)
    if len(seen_ids) > SEEN_LIMIT:
        seen_ids.clear()
        # shortcut: forgets old IDs after 1000 events, enough for one lesson; use SQLite in T15.

    # Count added
    counts.setdefault(nid, {"added": 0, "duplicate": 0})
    counts[nid]["added"] += 1

    # Push to recent
    recent.appendleft({
        "event_id": eid,
        "node_id": nid,
        "event_type": ev["event_type"],
        "recorded_at": ev["recorded_at"],
        "activity": ev["activity"],
        "values": ev["data"],
        "received_at": now
    })

    # Append to history
    history.setdefault(nid, deque(maxlen=HISTORY_SIZE))
    history[nid].append({
        "t": now,
        "recorded_at": ev["recorded_at"],
        "event_type": ev["event_type"],
        "activity": ev["activity"],
        "data": ev["data"]
    })

    # Update latest - any valid event proves node is alive
    latest.setdefault(nid, {"received_at": now, "values": {}})
    latest[nid]["received_at"] = now

    # Only update recorded_at/values if newer or equal
    if ev["recorded_at"] >= latest[nid].get("recorded_at", 0):
        latest[nid]["recorded_at"] = ev["recorded_at"]
        latest[nid]["event_type"] = ev["event_type"]
        latest[nid]["activity"] = ev["activity"]
        # Merge data into values
        for k, v in ev["data"].items():
            latest[nid]["values"][k] = v

    return "added"

def record_error(reason, raw, source_ip, example_type=None):
    """Record a rejected message."""
    totals["error_id"] += 1
    totals["rejected"] += 1
    hint = get_hint(reason)
    example = EXAMPLES.get(example_type, EXAMPLES["status"])
    error = {
        "id": totals["error_id"],
        "time": time.time(),
        "source": source_ip,
        "reason": reason,
        "hint": hint,
        "raw": raw,
        "example": example
    }
    errors.appendleft(error)
    log_message(f"rejected from {source_ip}: {reason}")

def get_lan_ip():
    """Get LAN IP using UDP connect trick."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

# ============================================================
# HTTP HANDLER
# ============================================================
class DashboardHandler(BaseHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        self.server_version = "LUMPI-Dashboard/1.0"
        super().__init__(*args, **kwargs)

    def log_message(self, format, *args):
        # Silenced: page polls every second, normal lines would hide real problems
        pass

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        # Static files
        if path in PAGE_FILES:
            filepath, content_type = PAGE_FILES[path]
            try:
                with open(filepath, 'rb') as f:
                    content = f.read()
                self.send_response(200)
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(content)))
                self.end_headers()
                self.wfile.write(content)
            except FileNotFoundError:
                self.send_error(404)
            return

        # API: /api/dashboard/nodes
        if path == '/api/dashboard/nodes':
            now = time.time()
            nodes = []
            # SHOWN_NODES plus any known nodes
            all_nodes = set(SHOWN_NODES) | set(latest.keys())
            for nid in sorted(all_nodes):
                nodes.append(format_node(nid, now))
            self.send_json(nodes)
            return

        # API: /api/dashboard/events
        if path == '/api/dashboard/events':
            self.send_json(list(recent))
            return

        # API: /api/dashboard/errors
        if path == '/api/dashboard/errors':
            self.send_json({
                "rejected_total": totals["rejected"],
                "errors": list(errors)
            })
            return

        # API: /api/dashboard/history
        if path == '/api/dashboard/history':
            qs = parse_qs(parsed.query)
            minutes = qs.get('minutes', ['15'])[0]
            try:
                minutes = int(minutes)
            except ValueError:
                self.send_json({"error": "minutes must be integer"}, 400)
                return
            if minutes not in HISTORY_MINUTES:
                self.send_json({"error": f"minutes must be one of {HISTORY_MINUTES}"}, 400)
                return

            now = time.time()
            nodes_data = {}
            for nid in [0, 1, 2]:
                node_hist = history.get(nid, deque())
                # Filter to requested minutes
                cutoff = now - minutes * 60
                filtered = [d for d in node_hist if d["recorded_at"] >= cutoff]
                nodes_data[str(nid)] = filtered

            self.send_json({
                "now": now,
                "minutes": minutes,
                "dark_lux": DARK_LUX,
                "stale_after_s": STALE_AFTER_S,
                "nodes": nodes_data
            })
            return

        # 404 for anything else
        self.send_json({"error": "not found"}, 404)

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path != '/api/telemetry':
            self.send_json({"error": "not found"}, 404)
            return

        # 1. Token check
        token = self.headers.get('X-LUMPI-Token', '')
        lan_key = os.environ.get('LUMPI_LAN_KEY', '')
        if not hmac.compare_digest(token, lan_key):
            record_error("bad token", "", self.client_address[0])
            self.send_json({"error": "bad token"}, 401)
            return

        # 2. Content-Length
        cl = self.headers.get('Content-Length')
        if cl is None:
            record_error("bad length: got None", "", self.client_address[0])
            self.send_json({"error": "bad length"}, 400)
            return
        try:
            content_length = int(cl)
        except ValueError:
            record_error(f"bad length: got {cl}", "", self.client_address[0])
            self.send_json({"error": "bad length"}, 400)
            return
        if content_length < 0 or content_length > MAX_BODY_BYTES:
            record_error(f"bad length: got {content_length}", "", self.client_address[0])
            self.send_json({"error": "bad length"}, 400)
            return

        # 3. Read and parse body
        body = self.rfile.read(content_length)
        try:
            text = body.decode('utf-8', errors='replace')
            ev = json.loads(text)
        except json.JSONDecodeError:
            record_error("not JSON", text[:200], self.client_address[0])
            self.send_json({"error": "not JSON"}, 400)
            return

        # 4. Check event
        error = check_event(ev)
        if error:
            record_error(error, text[:200], self.client_address[0], ev.get("event_type"))
            self.send_json({"error": error}, 400)
            return

        # 5. Add event
        now = time.time()
        result = add_event(ev, now, self.client_address[0])

        # 6. Record if --record
        if hasattr(self.server, 'record_file') and self.server.record_file:
            self.server.record_file.write(json.dumps({
                "delay": 0,  # delay not tracked here
                "received_at": now,
                "source": self.client_address[0],
                "event": ev,
                "expect": result
            }) + "\n")
            self.server.record_file.flush()

        self.send_json({"result": result}, 200)

    def send_json(self, data, status=200):
        content = json.dumps(data).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(content)))
        self.end_headers()
        self.wfile.write(content)

# ============================================================
# MAIN
# ============================================================
def main():
    # Check LUMPI_LAN_KEY
    lan_key = os.environ.get('LUMPI_LAN_KEY')
    if not lan_key:
        print("LUMPI_LAN_KEY environment variable is required", file=sys.stderr)
        print("Example: LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py", file=sys.stderr)
        sys.exit(1)

    # --record argument
    record_file = None
    if '--record' in sys.argv:
        idx = sys.argv.index('--record')
        if idx + 1 < len(sys.argv):
            record_path = sys.argv[idx + 1]
            record_file = open(record_path, 'a', encoding='utf-8')

    # Line buffering for log files
    sys.stdout.reconfigure(line_buffering=True)

    # Create server with address reuse
    class ReuseAddrServer(HTTPServer):
        def server_bind(self):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            super().server_bind()

    server = ReuseAddrServer(('', PORT), DashboardHandler)
    server.record_file = record_file

    # Print startup info
    lan_ip = get_lan_ip()
    print(f"dashboard on http://localhost:{PORT}")
    print(f"firmware URL: http://{lan_ip}:{PORT}/api/telemetry")

    # Signal handling
    def shutdown(signum, frame):
        print("\nshutting down...")
        if record_file:
            record_file.close()
        server.shutdown()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # Run
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if record_file:
            record_file.close()

if __name__ == '__main__':
    main()