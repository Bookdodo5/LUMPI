#!/usr/bin/env python3
"""LUMPI teacher dashboard server: checks nest events, keeps the latest state, serves the page."""

import argparse
import hmac
import json
import math
import os
import socket
import sys
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

PORT = 8204
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

REQUIRED_KEYS = ["event_id", "node_id", "event_type", "recorded_at", "activity", "data"]
EVENT_TYPES = ["start", "status", "room"]
REQUIRED_DATA = {
    "room": ["lux", "distance_cm"],
    "start": ["uid", "passage", "attempt", "fsr"],
    "status": ["uid", "passage", "attempt", "fsr"],
}
ACTIVITIES = ["idle", "book_on", "recording", "waiting_ai", "feedback", "quiet_study", "stopped"]

EXAMPLES = {
    "room": {
        "event_id": "lumpi_1a2b3c4d/42", "node_id": 0, "event_type": "room",
        "recorded_at": 1794794400, "activity": None,
        "data": {"lux": 230.5, "distance_cm": 160.0},
    },
    "start": {
        "event_id": "lumpi_1a2b3c4d/43", "node_id": 1, "event_type": "start",
        "recorded_at": 1794794401, "activity": "book_on",
        "data": {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "fsr": 3100},
    },
    "status": {
        "event_id": "lumpi_1a2b3c4d/44", "node_id": 1, "event_type": "status",
        "recorded_at": 1794794411, "activity": "idle",
        "data": {"uid": None, "passage": None, "attempt": None, "fsr": 12},
    },
}

# First match wins. Matched against the error text before the first ":".
HINTS = [
    ("bad token", "Send the header X-LUMPI-Token with the same key as LUMPI_LAN_KEY on the laptop."),
    ("bad length", "Send a Content-Length header between 1 and 2048 bytes."),
    ("not JSON", "The body must be one valid JSON object, encoded as UTF-8."),
    ("is not a JSON object", "Send one JSON object {...}, not a list or a single value."),
    ("missing", "Add the field named in the error. Every packet needs all keys in the example."),
    ("bad event_id", "event_id must be text with a '/' in it, shorter than 96 characters, like lumpi_1a2b3c4d/43."),
    ("bad node_id", "node_id must be the number 0, 1, 2 or 3, not text and not true."),
    ("bad event_type", "event_type must be start, status or room."),
    ("room event must come from node 0", "Only the nest (node_id 0) sends room events. Check NODE_ID in the firmware."),
    ("doll event cannot use node 0", "Node 0 is the nest and sends only room events. Dolls use node_id 1 or 2."),
    ("bad recorded_at", "Send whole Unix seconds after 1700000000. A clock that is not set yet sends small numbers."),
    ("bad activity", "Use one of the activity names from the list, or null."),
    ("data is not an object", "data must be a JSON object {...}."),
    ("bad data", "Each value must be a number, text or null. Send null for a failed sensor."),
]
DEFAULT_HINT = "Compare the packet with the correct example below."

PAGE_FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/style.css": ("style.css", "text/css; charset=utf-8"),
    "/app.js": ("app.js", "application/javascript; charset=utf-8"),
    "/charts.js": ("charts.js", "application/javascript; charset=utf-8"),
}
DASHBOARD_DIR = Path(__file__).resolve().parent.parent / "dashboard"

# In-memory state. No database: after a restart every card is UNKNOWN until the nest sends again.
latest = {}                         # node_id -> {received_at, recorded_at, event_type, activity, values{}}
recent = deque(maxlen=RECENT_SIZE)  # newest first, each event + "received_at"
seen_ids = set()                    # duplicate check
history = {}                        # node_id -> deque of {t, recorded_at, event_type, activity, data}
counts = {}                         # node_id -> {"added": n, "duplicate": n}
errors = deque(maxlen=ERROR_SIZE)   # newest first
totals = {"rejected": 0, "error_id": 0}

LAN_KEY = None        # set from LUMPI_LAN_KEY in main(); tests set it directly
record_file = None    # open file for --record, or None
last_record_time = None


def reset_state():
    latest.clear()
    recent.clear()
    seen_ids.clear()
    history.clear()
    counts.clear()
    errors.clear()
    totals["rejected"] = 0
    totals["error_id"] = 0


def json_text(value):
    return json.dumps(value)[:60]


def is_plain_value(value):
    # Values are shown as plain text, so only numbers, text and null are allowed.
    if value is None or type(value) is str or type(value) is int:
        return True
    return type(value) is float and math.isfinite(value)


def check_event(event):
    """Return the first error text, or None when the event is good."""
    if not isinstance(event, dict):
        return "event is not a JSON object"
    for key in REQUIRED_KEYS:
        if key not in event:
            return f"missing {key}"

    event_id = event["event_id"]
    if not (isinstance(event_id, str) and "/" in event_id and len(event_id) < 96):
        return f"bad event_id: got {json_text(event_id)}"

    # type() check on purpose: True is an int in Python, but not a valid node.
    node_id = event["node_id"]
    if type(node_id) is not int or node_id not in VALID_NODES:
        return f"bad node_id: got {json_text(node_id)}"

    event_type = event["event_type"]
    if event_type not in EVENT_TYPES:
        return f"bad event_type: got {json_text(event_type)}"
    if event_type == "room" and node_id != 0:
        return f"room event must come from node 0: got node {node_id}"
    if event_type != "room" and node_id == 0:
        return f"doll event cannot use node 0: got {event_type}"

    recorded_at = event["recorded_at"]
    if type(recorded_at) is not int or recorded_at <= VALID_CLOCK_AFTER:
        return f"bad recorded_at: got {json_text(recorded_at)}"

    activity = event["activity"]
    if activity is not None and activity not in ACTIVITIES:
        return f"bad activity: got {json_text(activity)}"

    data = event["data"]
    if not isinstance(data, dict):
        return f"data is not an object: got {json_text(data)}"
    for key in REQUIRED_DATA[event_type]:
        if key not in data:
            return f"missing data.{key}"
    for key, value in data.items():
        if not is_plain_value(value):
            return f"bad data.{key}: got {json_text(value)}"
    return None


def counts_for(node_id):
    if node_id not in counts:
        counts[node_id] = {"added": 0, "duplicate": 0}
    return counts[node_id]


def history_for(node_id):
    if node_id not in history:
        history[node_id] = deque(maxlen=HISTORY_SIZE)
    return history[node_id]


def add_event(event, now):
    """Store a checked event. Returns "added" or "duplicate"."""
    node_id = event["node_id"]
    node_counts = counts_for(node_id)

    if event["event_id"] in seen_ids:
        node_counts["duplicate"] += 1
        return "duplicate"
    # shortcut: forgets old IDs after 1000 events, enough for one lesson; use SQLite in T15.
    if len(seen_ids) >= SEEN_LIMIT:
        seen_ids.clear()
    seen_ids.add(event["event_id"])

    node_counts["added"] += 1
    recent.appendleft({**event, "received_at": now})
    history_for(node_id).append({
        "t": now,
        "recorded_at": event["recorded_at"],
        "event_type": event["event_type"],
        "activity": event["activity"],
        "data": event["data"],
    })

    state = latest.get(node_id)
    if state is None:
        state = {"received_at": now, "recorded_at": 0, "event_type": None, "activity": None, "values": {}}
        latest[node_id] = state
    # Any valid event proves the node is alive.
    state["received_at"] = now
    # A late, older event (Wi-Fi retry) must not overwrite newer values.
    if event["recorded_at"] >= state["recorded_at"]:
        state["recorded_at"] = event["recorded_at"]
        state["event_type"] = event["event_type"]
        state["activity"] = event["activity"]
        state["values"].update(event["data"])
    return "added"


def node_status(age_s):
    if age_s is None:
        return "unknown"
    if age_s < STALE_AFTER_S:
        return "online"
    if age_s < OFFLINE_AFTER_S:
        return "stale"
    return "offline"


def is_dark(lux):
    return isinstance(lux, (int, float)) and lux < DARK_LUX


def node_item(node_id, now):
    node_counts = dict(counts_for(node_id))
    state = latest.get(node_id)
    if state is None:
        return {"node_id": node_id, "status": "unknown", "counts": node_counts}
    age = now - state["received_at"]
    item = {
        "node_id": node_id,
        "status": node_status(age),
        "age_s": round(age, 1),
        "last_seen": state["received_at"],
        "recorded_at": state["recorded_at"],
        "event_type": state["event_type"],
        "activity": state["activity"],
        "values": dict(state["values"]),
        "counts": node_counts,
    }
    if node_id == 0:
        item["low_light"] = is_dark(state["values"].get("lux"))
    return item


def nodes_payload(now):
    node_ids = sorted(set(SHOWN_NODES) | set(latest.keys()))
    return [node_item(node_id, now) for node_id in node_ids]


def events_payload():
    return list(recent)


def errors_payload():
    return {"rejected_total": totals["rejected"], "errors": list(errors)}


def history_payload(minutes, now):
    cutoff = now - minutes * 60
    nodes = {}
    for node_id in SHOWN_NODES:
        nodes[str(node_id)] = [p for p in history.get(node_id, []) if p["t"] >= cutoff]
    return {
        "now": now,
        "minutes": minutes,
        "dark_lux": DARK_LUX,
        "stale_after_s": STALE_AFTER_S,
        "nodes": nodes,
    }


def hint_for(reason):
    key = reason.split(":", 1)[0]
    for text, hint in HINTS:
        if text in key:
            return hint
    return DEFAULT_HINT


def record_error(reason, source, raw, event, now):
    totals["rejected"] += 1
    totals["error_id"] += 1
    event_type = event.get("event_type") if isinstance(event, dict) else None
    if isinstance(event_type, str) and event_type in EXAMPLES:
        example = EXAMPLES[event_type]
    else:
        example = EXAMPLES["status"]
    errors.appendleft({
        "id": totals["error_id"],
        "time": now,
        "source": source,
        "reason": reason,
        "hint": hint_for(reason),
        "raw": raw,
        "example": example,
    })
    print(f"rejected from {source}: {reason}", flush=True)


def record_message(now, source, expect, event=None, raw=None):
    """Append one checked message to the --record file in scenario format."""
    global last_record_time
    if record_file is None:
        return
    delay = 0.0 if last_record_time is None else now - last_record_time
    last_record_time = now
    line = {"delay": round(delay, 3), "received_at": round(now, 3), "source": source, "expect": expect}
    if event is not None:
        line["event"] = event
    else:
        line["raw"] = raw
    record_file.write(json.dumps(line) + "\n")
    record_file.flush()


def parse_length(text):
    try:
        length = int(text)
    except (TypeError, ValueError):
        return None
    if length < 0 or length > MAX_BODY_BYTES:
        return None
    return length


def handle_telemetry(token, length_text, body, source, now):
    """Handle one POST /api/telemetry. Returns (http status, json payload)."""
    token_ok = bool(LAN_KEY) and hmac.compare_digest((token or "").encode("utf-8"), LAN_KEY.encode("utf-8"))
    if not token_ok:
        # Raw is empty on purpose: a stranger must not be able to put text on the page.
        record_error("bad token", source, "", None, now)
        return 401, {"error": "bad token"}

    if parse_length(length_text) is None:
        reason = f"bad length: got {length_text or 'none'}"
        record_error(reason, source, "", None, now)
        return 400, {"error": reason}

    text = body.decode("utf-8", errors="replace")
    try:
        event = json.loads(text)
    except (ValueError, RecursionError):
        record_error("not JSON", source, text, None, now)
        record_message(now, source, "rejected", raw=text)
        return 400, {"error": "not JSON"}

    reason = check_event(event)
    if reason is not None:
        record_error(reason, source, text, event, now)
        record_message(now, source, "rejected", event=event)
        return 400, {"error": reason}

    result = add_event(event, now)
    record_message(now, source, result, event=event)
    return 200, {"result": result}


class DashboardHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # The page polls every second; normal request lines would hide real problems.
        pass

    def send_bytes(self, status, content_type, body):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_bytes(status, "application/json; charset=utf-8", body)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path in PAGE_FILES:
            file_name, content_type = PAGE_FILES[url.path]
            self.send_bytes(200, content_type, (DASHBOARD_DIR / file_name).read_bytes())
        elif url.path == "/api/dashboard/nodes":
            self.send_json(200, nodes_payload(time.time()))
        elif url.path == "/api/dashboard/events":
            self.send_json(200, events_payload())
        elif url.path == "/api/dashboard/errors":
            self.send_json(200, errors_payload())
        elif url.path == "/api/dashboard/history":
            minutes_text = parse_qs(url.query).get("minutes", ["15"])[0]
            if not minutes_text.isdigit() or int(minutes_text) not in HISTORY_MINUTES:
                self.send_json(400, {"error": "minutes must be 5, 15 or 60"})
            else:
                self.send_json(200, history_payload(int(minutes_text), time.time()))
        else:
            self.send_json(404, {"error": "not found"})

    def do_POST(self):
        if urlparse(self.path).path != "/api/telemetry":
            self.send_json(404, {"error": "not found"})
            return
        length_text = self.headers.get("Content-Length")
        length = parse_length(length_text)
        body = self.rfile.read(length) if length is not None else None
        status, payload = handle_telemetry(
            self.headers.get("X-LUMPI-Token"), length_text, body, self.client_address[0], time.time()
        )
        self.send_json(status, payload)


def lan_address():
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))  # UDP connect sends nothing; it only picks the LAN interface
            return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"


def main():
    global LAN_KEY, record_file
    parser = argparse.ArgumentParser(description="LUMPI teacher dashboard server")
    parser.add_argument("--record", metavar="FILE", help="append every checked message to a scenario file")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)  # a log file stays complete after a crash

    LAN_KEY = os.environ.get("LUMPI_LAN_KEY")
    if not LAN_KEY:
        print("LUMPI_LAN_KEY is not set. Start with: LUMPI_LAN_KEY=<key> python3 laptop/dashboard_server.py")
        sys.exit(1)
    if args.record:
        record_file = open(args.record, "a", encoding="utf-8")

    server = HTTPServer(("0.0.0.0", PORT), DashboardHandler)
    print(f"dashboard on http://localhost:{PORT}")
    print(f"firmware: LUMPI_ADAPTER_URL=http://{lan_address()}:{PORT}/api/telemetry")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
