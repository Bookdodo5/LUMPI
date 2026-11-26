#!/usr/bin/env python3
"""
LUMPI Teacher Dashboard server.
Plain Python stdlib: http.server, json, hmac, collections.deque, socket, sys, os, time, threading.
"""

import os
import sys
import json
import hmac
import time
import socket
import threading
from collections import deque
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

# ===================== CONSTANTS (top of file) =====================
PORT = 8203
MAX_BODY_BYTES = 2048
STALE_AFTER_S = 30        # 3 missed 10 s heartbeats
OFFLINE_AFTER_S = 60
RECENT_SIZE = 20          # recent events table
ERROR_SIZE = 50           # rejected messages kept
SEEN_LIMIT = 1000         # event IDs remembered for duplicate check
VALID_CLOCK_AFTER = 1700000000
HISTORY_SIZE = 720        # one hour at one message per 5 s, per node
HISTORY_MINUTES = [5, 15, 60]
DARK_LUX = 100.0
SHOWN_NODES = [0, 1, 2]   # always listed, even before any data
VALID_NODES = [0, 1, 2, 3]

# Required data keys per event type
REQUIRED_DATA = {
    "room": ["lux", "distance_cm"],
    "start": ["uid", "passage", "attempt", "fsr"],
    "status": ["uid", "passage", "attempt", "fsr"],
}

# Valid activities
ACTIVITIES = [
    "idle", "book_on", "recording", "waiting_ai",
    "feedback", "quiet_study", "stopped", None
]

# Example packets for error display
EXAMPLES = {
    "room": {
        "event_id": "lumpi_1a2b3c4d/42",
        "node_id": 0,
        "event_type": "room",
        "recorded_at": 1794794400,
        "activity": None,
        "data": {"lux": 230.5, "distance_cm": 160.0}
    },
    "start": {
        "event_id": "lumpi_1a2b3c4d/43",
        "node_id": 1,
        "event_type": "start",
        "recorded_at": 1794794401,
        "activity": "book_on",
        "data": {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "fsr": 3100}
    },
    "status": {
        "event_id": "lumpi_1a2b3c4d/44",
        "node_id": 1,
        "event_type": "status",
        "recorded_at": 1794794411,
        "activity": "recording",
        "data": {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "fsr": 3124}
    },
}

# Hints for error messages: (substring to match, fix hint)
HINTS = [
    ("event is not a JSON object", "Send one JSON object {...}, not a list or a single value."),
    ("missing event_id", "Include \"event_id\": \"gateway/run-number\"."),
    ("missing node_id", "Include \"node_id\": 0 for nest, 1–3 for dolls."),
    ("missing event_type", "Include \"event_type\": \"room\" or \"start\" or \"status\"."),
    ("missing recorded_at", "Include \"recorded_at\": <unix seconds>."),
    ("missing activity", "Include \"activity\": \"idle\" or other valid state, or null."),
    ("missing data", "Include \"data\": {...} object."),
    ("bad event_id", "event_id must be a string containing \"/\" and shorter than 96 chars."),
    ("bad node_id", "node_id must be 0, 1, 2, or 3 (integer, not bool or string)."),
    ("bad event_type", "event_type must be \"room\", \"start\", or \"status\"."),
    ("room event must come from node 0", "Set NODE_ID=0 in nest firmware for room events."),
    ("doll event cannot use node 0", "Set NODE_ID=1, 2, or 3 in doll firmware."),
    ("bad recorded_at", "recorded_at must be whole unix seconds > 1700000000 (year 2023+)."),
    ("bad activity", "activity must be one of: idle, book_on, recording, waiting_ai, feedback, quiet_study, stopped, or null."),
    ("data is not an object", "data must be a JSON object {...}, not a string or array."),
    ("missing data.lux", "Include \"lux\": <number> in data."),
    ("missing data.distance_cm", "Include \"distance_cm\": <number> in data."),
    ("missing data.uid", "Include \"uid\": \"tag UID string\" in data."),
    ("missing data.passage", "Include \"passage\": <number> in data."),
    ("missing data.attempt", "Include \"attempt\": <number> in data."),
    ("missing data.fsr", "Include \"fsr\": <number> in data."),
    ("bad data.lux", "lux must be a number or null."),
    ("bad data.distance_cm", "distance_cm must be a number or null."),
    ("bad data.uid", "uid must be a string or null."),
    ("bad data.passage", "passage must be a number or null."),
    ("bad data.attempt", "attempt must be a number or null."),
    ("bad data.fsr", "fsr must be a number or null."),
    ("bad token", "Header X-LUMPI-Token must match LUMPI_LAN_KEY."),
    ("bad length", "Content-Length must be <= 2048."),
    ("not JSON", "Body must be valid JSON."),
]

# Static files to serve
PAGE_FILES = {
    "/": ("dashboard/index.html", "text/html; charset=utf-8"),
    "/style.css": ("dashboard/style.css", "text/css; charset=utf-8"),
    "/app.js": ("dashboard/app.js", "application/javascript; charset=utf-8"),
    "/charts.js": ("dashboard/charts.js", "application/javascript; charset=utf-8"),
}

# ===================== IN-MEMORY STATE =====================
latest = {}                         # node_id -> {received_at, recorded_at, event_type, activity, values{}}
recent = deque(maxlen=RECENT_SIZE)  # newest first, each event + "received_at"
seen_ids = set()                    # duplicate check
history = {}                        # node_id -> deque(maxlen=HISTORY_SIZE) of {t, recorded_at, event_type, activity, data}
counts = {}                         # node_id -> {"added": n, "duplicate": n}
errors = deque(maxlen=ERROR_SIZE)   # newest first
totals = {"rejected": 0, "error_id": 0}

# ===================== HELPER FUNCTIONS =====================
def get_lan_ip():
    """Find LAN IP using UDP connect trick (sends nothing)."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def now_ts():
    return time.time()

def node_status(age_s):
    if age_s is None:
        return "unknown"
    if age_s < STALE_AFTER_S:
        return "online"
    if age_s < OFFLINE_AFTER_S:
        return "stale"
    return "offline"

def ensure_node(node_id):
    if node_id not in latest:
        latest[node_id] = {
            "received_at": None,
            "recorded_at": None,
            "event_type": None,
            "activity": None,
            "values": {}
        }
    if node_id not in history:
        history[node_id] = deque(maxlen=HISTORY_SIZE)
    if node_id not in counts:
        counts[node_id] = {"added": 0, "duplicate": 0}

def add_to_history(node_id, recorded_at, event_type, activity, data):
    ensure_node(node_id)
    t = now_ts()
    history[node_id].append({
        "t": t,
        "recorded_at": recorded_at,
        "event_type": event_type,
        "activity": activity,
        "data": dict(data) if data else {}
    })

def find_hint(reason):
    for substr, hint in HINTS:
        if substr in reason:
            return hint
    return "Check the event format against the examples."

def check_event(ev):
    """Return first error text, or None if valid."""
    # 1. not a dict
    if not isinstance(ev, dict):
        return "event is not a JSON object"
    # 2. required keys present
    for key in ("event_id", "node_id", "event_type", "recorded_at", "activity", "data"):
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
    if etype not in ("room", "start", "status"):
        return f"bad event_type: got {json.dumps(etype)[:60]}"
    # 6. room only from node 0
    if etype == "room" and nid != 0:
        return f"room event must come from node 0: got node {nid}"
    # 7. doll event cannot use node 0
    if etype in ("start", "status") and nid == 0:
        return f"doll event cannot use node 0: got {etype}"
    # 8. recorded_at
    rat = ev["recorded_at"]
    if not isinstance(rat, int) or rat <= VALID_CLOCK_AFTER:
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
    for rkey in required:
        if rkey not in data:
            return f"missing data.{rkey}"
    # 12. each value plain (number, string, or null)
    for dkey, dval in data.items():
        if not (dval is None or isinstance(dval, (int, float, str))):
            return f"bad data.{dkey}: got {json.dumps(dval)[:60]}"
    return None

def add_event(ev):
    """Save a valid event. Returns 'added' or 'duplicate'."""
    eid = ev["event_id"]
    if eid in seen_ids:
        nid = ev["node_id"]
        counts[nid]["duplicate"] += 1
        return "duplicate"
    seen_ids.add(eid)
    if len(seen_ids) > SEEN_LIMIT:
        seen_ids.clear()
        # shortcut: forgets old IDs after 1000 events, enough for one lesson; use SQLite in T15.

    nid = ev["node_id"]
    ensure_node(nid)
    recv_at = now_ts()
    counts[nid]["added"] += 1

    # push to recent
    recent.appendleft({
        "event_id": eid,
        "node_id": nid,
        "event_type": ev["event_type"],
        "recorded_at": ev["recorded_at"],
        "activity": ev["activity"],
        "data": ev["data"],
        "received_at": recv_at
    })

    # history
    add_to_history(nid, ev["recorded_at"], ev["event_type"], ev["activity"], ev["data"])

    # latest
    l = latest[nid]
    l["received_at"] = recv_at
    # only update if recorded_at is newer or equal
    if l["recorded_at"] is None or ev["recorded_at"] >= l["recorded_at"]:
        l["recorded_at"] = ev["recorded_at"]
        l["event_type"] = ev["event_type"]
        l["activity"] = ev["activity"]
        # merge data into values
        for k, v in ev["data"].items():
            l["values"][k] = v
    return "added"

def record_error(reason, source_ip, raw_body, example_key=None):
    """Record an error for the developer panel."""
    totals["rejected"] += 1
    totals["error_id"] += 1
    err_id = totals["error_id"]
    example = EXAMPLES.get(example_key, EXAMPLES["status"])
    hint = find_hint(reason)
    errors.appendleft({
        "id": err_id,
        "time": now_ts(),
        "source": source_ip,
        "reason": reason,
        "hint": hint,
        "raw": raw_body,
        "example": example
    })

def build_node_item(node_id, now):
    """Build node item for /api/dashboard/nodes."""
    ensure_node(node_id)
    l = latest[node_id]
    cnt = counts[node_id]
    age = None
    if l["received_at"] is not None:
        age = now - l["received_at"]
    status = node_status(age)
    item = {
        "node_id": node_id,
        "status": status,
        "counts": {"added": cnt["added"], "duplicate": cnt["duplicate"]}
    }
    if age is not None:
        item["age_s"] = round(age, 1)
        item["last_seen"] = round(l["received_at"], 1)
    if l["recorded_at"] is not None:
        item["recorded_at"] = l["recorded_at"]
        item["event_type"] = l["event_type"]
        item["activity"] = l["activity"]
        item["values"] = dict(l["values"])
    if node_id == 0:
        lux = l["values"].get("lux")
        item["low_light"] = lux is not None and lux < DARK_LUX
    return item

# ===================== HTTP HANDLER =====================
class DashboardHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Silenced: page polls every second; normal request lines would hide real problems
        pass

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        # Static files
        if path in PAGE_FILES:
            fname, ctype = PAGE_FILES[path]
            try:
                with open(fname, "rb") as f:
                    content = f.read()
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(content)))
                self.end_headers()
                self.wfile.write(content)
            except FileNotFoundError:
                self.send_error(404, "Not found")
            return

        # API endpoints
        if path == "/api/dashboard/nodes":
            now = now_ts()
            nodes = []
            for nid in SHOWN_NODES:
                nodes.append(build_node_item(nid, now))
            # also include any known nodes beyond SHOWN_NODES
            for nid in latest:
                if nid not in SHOWN_NODES:
                    nodes.append(build_node_item(nid, now))
            self.send_json(nodes)
            return

        if path == "/api/dashboard/events":
            self.send_json(list(recent))
            return

        if path == "/api/dashboard/errors":
            self.send_json({
                "rejected_total": totals["rejected"],
                "errors": list(errors)
            })
            return

        if path == "/api/dashboard/history":
            qs = parse_qs(parsed.query)
            minutes = qs.get("minutes", ["15"])[0]
            try:
                minutes = int(minutes)
            except ValueError:
                self.send_error(400, "bad minutes")
                return
            if minutes not in HISTORY_MINUTES:
                self.send_error(400, "bad minutes")
                return
            now = now_ts()
            cutoff = now - minutes * 60
            nodes_data = {}
            for nid in SHOWN_NODES:
                ensure_node(nid)
                points = []
                for p in history[nid]:
                    if p["t"] >= cutoff:
                        points.append(p)
                nodes_data[str(nid)] = points
            self.send_json({
                "now": round(now, 3),
                "minutes": minutes,
                "dark_lux": DARK_LUX,
                "stale_after_s": STALE_AFTER_S,
                "nodes": nodes_data
            })
            return

        # anything else 404
        self.send_error(404, "Not found")

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path != "/api/telemetry":
            self.send_error(404, "Not found")
            return

        # 1. Token check
        token = self.headers.get("X-LUMPI-Token", "")
        expected = os.environ.get("LUMPI_LAN_KEY", "")
        if not expected:
            self.send_error(500, "LUMPI_LAN_KEY not set")
            return
        if not hmac.compare_digest(token, expected):
            record_error("bad token", self.client_address[0], "", "status")
            self.send_json({"error": "bad token"}, 401)
            print(f"rejected from {self.client_address[0]}: bad token", flush=True)
            return

        # 2. Content-Length
        cl = self.headers.get("Content-Length")
        if cl is None:
            record_error("bad length: got None", self.client_address[0], "", "status")
            self.send_json({"error": "bad length: got None"}, 400)
            print(f"rejected from {self.client_address[0]}: bad length: got None", flush=True)
            return
        try:
            length = int(cl)
        except ValueError:
            record_error(f"bad length: got {cl}", self.client_address[0], "", "status")
            self.send_json({"error": f"bad length: got {cl}"}, 400)
            print(f"rejected from {self.client_address[0]}: bad length: got {cl}", flush=True)
            return
        if length < 0 or length > MAX_BODY_BYTES:
            record_error(f"bad length: got {length}", self.client_address[0], "", "status")
            self.send_json({"error": f"bad length: got {length}"}, 400)
            print(f"rejected from {self.client_address[0]}: bad length: got {length}", flush=True)
            return

        # 3. Read and parse body
        body_bytes = self.rfile.read(length)
        try:
            body_text = body_bytes.decode("utf-8", errors="replace")
        except Exception:
            body_text = "(could not decode body)"
        try:
            ev = json.loads(body_text)
        except json.JSONDecodeError:
            record_error("not JSON", self.client_address[0], body_text, "status")
            self.send_json({"error": "not JSON"}, 400)
            print(f"rejected from {self.client_address[0]}: not JSON", flush=True)
            return

        # 4. Check event
        err = check_event(ev)
        if err:
            # For validation errors, use the event_type to pick example if available
            example_key = ev.get("event_type") if isinstance(ev, dict) else None
            record_error(err, self.client_address[0], body_text, example_key)
            self.send_json({"error": err}, 400)
            print(f"rejected from {self.client_address[0]}: {err}", flush=True)
            return

        # 5. Add event
        result = add_event(ev)
        self.send_json({"result": result})

    def send_json(self, obj, status=200):
        data = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

# ===================== MAIN =====================
def run_server(record_file=None):
    if "LUMPI_LAN_KEY" not in os.environ:
        print("LUMPI_LAN_KEY environment variable is required.", file=sys.stderr)
        print("Example: LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py", file=sys.stderr)
        sys.exit(1)

    # line buffering so a log file is complete after a crash
    sys.stdout.reconfigure(line_buffering=True)

    lan_ip = get_lan_ip()
    print(f"dashboard on http://localhost:{PORT}")
    print(f"firmware posts to http://{lan_ip}:{PORT}/api/telemetry")

    server = HTTPServer(("", PORT), DashboardHandler)

    # If record file specified, wrap the handler to record
    if record_file:
        original_do_POST = DashboardHandler.do_POST
        def recorded_do_POST(self):
            # Call original but capture result
            original_do_POST(self)
        DashboardHandler.do_POST = recorded_do_POST

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--record", help="Record all checked messages to a jsonl file")
    args = parser.parse_args()
    run_server(args.record)