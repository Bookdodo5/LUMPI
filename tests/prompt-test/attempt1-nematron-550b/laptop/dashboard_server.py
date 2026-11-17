#!/usr/bin/env python3
"""
LUMPI Teacher Dashboard server.
Serves static files and JSON API, checks events, keeps state in memory.
"""

import os
import sys
import json
import hmac
import time
import argparse
from collections import deque
from http.server import HTTPServer, BaseHTTPRequestHandler
from socket import socket, AF_INET, SOCK_DGRAM


# Constants (top of file)
PORT = 8201
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
    "room": ["lux", "distance_cm"],
    "start": ["uid", "passage", "attempt", "fsr"],
    "status": ["uid", "passage", "attempt", "fsr"],
}

# Valid activities
ACTIVITIES = ["idle", "book_on", "recording", "waiting_ai", "feedback", "quiet_study", "stopped"]

# Example packets for error display
EXAMPLES = {
    "room": {"event_id": "lumpi_1a2b3c4d/1", "node_id": 0, "event_type": "room", "recorded_at": 1794794400, "activity": None, "data": {"lux": 230.5, "distance_cm": 160.0}},
    "start": {"event_id": "lumpi_1a2b3c4d/2", "node_id": 1, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "fsr": 3100}},
    "status": {"event_id": "lumpi_1a2b3c4d/3", "node_id": 1, "event_type": "status", "recorded_at": 1794794411, "activity": "idle", "data": {"uid": None, "passage": 0, "attempt": 0, "fsr": 1200}},
}

# Hints for errors (text to match -> fix hint)
HINTS = [
    ("event is not a JSON object", "Send one JSON object {...}, not a list or a single value."),
    ("missing event_id", "Include \"event_id\": \"<gateway>/<number>\"."),
    ("missing node_id", "Include \"node_id\": 0 for nest or 1-3 for dolls."),
    ("missing event_type", "Include \"event_type\": \"room\", \"start\", or \"status\"."),
    ("missing recorded_at", "Include \"recorded_at\": <unix seconds>."),
    ("missing activity", "Include \"activity\": \"idle\" | \"book_on\" | ... or null."),
    ("missing data", "Include \"data\": {...} object."),
    ("bad event_id", "event_id must be a string containing \"/\" and shorter than 96 chars."),
    ("bad node_id", "node_id must be a whole number 0-3 (not true/false, not a string)."),
    ("bad event_type", "event_type must be \"room\", \"start\", or \"status\"."),
    ("room event must come from node 0", "Room events only from node 0 (the nest)."),
    ("doll event cannot use node 0", "Doll events must use node_id 1, 2, or 3."),
    ("bad recorded_at", "recorded_at must be whole unix seconds > 1700000000 (clock not set)."),
    ("bad activity", "activity must be one of: idle, book_on, recording, waiting_ai, feedback, quiet_study, stopped, or null."),
    ("data is not an object", "data must be an object {key: value}."),
    ("missing data.lux", "Room event data must include \"lux\": <number>."),
    ("missing data.distance_cm", "Room event data must include \"distance_cm\": <number>."),
    ("missing data.uid", "Doll event data must include \"uid\": <string or null>."),
    ("missing data.passage", "Doll event data must include \"passage\": <number>."),
    ("missing data.attempt", "Doll event data must include \"attempt\": <number>."),
    ("missing data.fsr", "Doll event data must include \"fsr\": <number>."),
    ("bad data.", "Values must be number, string, or null (no bool, list, object)."),
    ("bad token", "Header X-LUMPI-Token must match LUMPI_LAN_KEY."),
    ("bad length", "Content-Length must be present and <= 2048."),
    ("not JSON", "Body must be valid JSON."),
]

# Static files to serve
PAGE_FILES = {
    "/": ("dashboard/index.html", "text/html; charset=utf-8"),
    "/style.css": ("dashboard/style.css", "text/css; charset=utf-8"),
    "/app.js": ("dashboard/app.js", "application/javascript; charset=utf-8"),
    "/charts.js": ("dashboard/charts.js", "application/javascript; charset=utf-8"),
}

# In-memory state
latest = {}                         # node_id -> {received_at, recorded_at, event_type, activity, values{}}
recent = deque(maxlen=RECENT_SIZE)  # newest first, each event + "received_at"
seen_ids = set()                    # duplicate check
history = {}                        # node_id -> deque(maxlen=HISTORY_SIZE) of {t, recorded_at, event_type, activity, data}
counts = {}                         # node_id -> {"added": n, "duplicate": n}
errors = deque(maxlen=ERROR_SIZE)   # newest first
totals = {"rejected": 0, "error_id": 0}

# For --record
record_file = None
record_fp = None


def log_message(msg):
    """Print log line (silenced for normal requests to avoid poll pollution)."""
    pass  # silenced because page polls every second


def get_lan_ip():
    """Find LAN IP for the firmware config hint."""
    try:
        s = socket(AF_INET, SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def node_status(age_s):
    """Compute status from age in seconds."""
    if age_s is None:
        return "unknown"
    if age_s < STALE_AFTER_S:
        return "online"
    if age_s < OFFLINE_AFTER_S:
        return "stale"
    return "offline"


def check_event(evt):
    """Return first error text, or None if valid."""
    # 1. not a dict
    if not isinstance(evt, dict):
        return "event is not a JSON object"

    # 2. required keys present
    for key in ("event_id", "node_id", "event_type", "recorded_at", "activity", "data"):
        if key not in evt:
            return f"missing {key}"

    # 3. event_id
    eid = evt["event_id"]
    if not isinstance(eid, str) or "/" not in eid or len(eid) >= 96:
        return f"bad event_id: got {json.dumps(eid)[:60]}"

    # 4. node_id (bool rejected on purpose: True is an int in Python)
    nid = evt["node_id"]
    if isinstance(nid, bool) or not isinstance(nid, int) or nid not in VALID_NODES:
        return f"bad node_id: got {json.dumps(nid)[:60]}"

    # 5. event_type
    etype = evt["event_type"]
    if etype not in ("room", "start", "status"):
        return f"bad event_type: got {json.dumps(etype)[:60]}"

    # 6. room only from node 0
    if etype == "room" and nid != 0:
        return f"room event must come from node 0: got node {nid}"

    # 7. doll events never from node 0
    if etype in ("start", "status") and nid == 0:
        return f"doll event cannot use node 0: got {etype}"

    # 8. recorded_at
    rat = evt["recorded_at"]
    if not isinstance(rat, int) or rat <= VALID_CLOCK_AFTER:
        return f"bad recorded_at: got {json.dumps(rat)[:60]}"

    # 9. activity
    act = evt["activity"]
    if act is not None and act not in ACTIVITIES:
        return f"bad activity: got {json.dumps(act)[:60]}"

    # 10. data is object
    data = evt["data"]
    if not isinstance(data, dict):
        return f"data is not an object: got {json.dumps(data)[:60]}"

    # 11. required data keys
    required = REQUIRED_DATA.get(etype, [])
    for rkey in required:
        if rkey not in data:
            return f"missing data.{rkey}"

    # 12. each value plain (number, string, null)
    for dkey, dval in data.items():
        if not (isinstance(dval, (int, float, str)) or dval is None):
            return f"bad data.{dkey}: got {json.dumps(dval)[:60]}"

    return None


def add_event(evt, now, source):
    """Save a valid event. Returns 'added' or 'duplicate'."""
    eid = evt["event_id"]
    nid = evt["node_id"]

    # 1. Duplicate check
    if eid in seen_ids:
        counts[nid]["duplicate"] = counts[nid].get("duplicate", 0) + 1
        return "duplicate"

    # 2. Remember ID (clear at 1000; shortcut: forgets old IDs after 1000 events, enough for one lesson; use SQLite in T15.)
    seen_ids.add(eid)
    if len(seen_ids) > SEEN_LIMIT:
        seen_ids.clear()
        seen_ids.add(eid)

    # 3. Count added; push to recent; append to history
    if nid not in counts:
        counts[nid] = {"added": 0, "duplicate": 0}
    counts[nid]["added"] = counts[nid].get("added", 0) + 1
    recent.appendleft({"event": evt, "received_at": now})

    if nid not in history:
        history[nid] = deque(maxlen=HISTORY_SIZE)
    history[nid].append({"t": now, "recorded_at": evt["recorded_at"], "event_type": evt["event_type"], "activity": evt["activity"], "data": evt["data"]})

    # 4. received_at = now — any valid event proves the node is alive
    if nid not in latest:
        latest[nid] = {"received_at": now, "recorded_at": 0, "event_type": "", "activity": None, "values": {}}
    latest[nid]["received_at"] = now

    # 5. Only update values if recorded_at is newer or equal
    if evt["recorded_at"] >= latest[nid]["recorded_at"]:
        latest[nid]["recorded_at"] = evt["recorded_at"]
        latest[nid]["event_type"] = evt["event_type"]
        latest[nid]["activity"] = evt["activity"]
        # merge data into values
        for k, v in evt["data"].items():
            latest[nid]["values"][k] = v

    return "added"


def record_message(delay, received_at, source, event=None, raw=None, expect=None):
    """Write one line to the record file."""
    if record_fp is None:
        return
    line = {"delay": delay, "received_at": received_at, "source": source}
    if event is not None:
        line["event"] = event
    if raw is not None:
        line["raw"] = raw
    if expect is not None:
        line["expect"] = expect
    record_fp.write(json.dumps(line) + "\n")
    record_fp.flush()


def find_hint(reason):
    """Find hint for error reason (match before first colon)."""
    prefix = reason.split(":")[0] if ":" in reason else reason
    for match, hint in HINTS:
        if prefix.startswith(match):
            return hint
    return "Check the event format and required fields."


def get_example_for_error(reason):
    """Return example packet for the error's event type, or status if unknown."""
    if "event_type" in reason or "room event" in reason:
        return EXAMPLES["room"]
    if "doll event" in reason or "activity" in reason or "data.uid" in reason or "data.fsr" in reason:
        return EXAMPLES["start"]
    return EXAMPLES["status"]


class DashboardHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        # Static files
        if self.path in PAGE_FILES:
            filepath, ctype = PAGE_FILES[self.path]
            try:
                with open(filepath, "rb") as f:
                    content = f.read()
                self.send_response(200)
                self.send_header("Content-Type", ctype)
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(content)
                return
            except FileNotFoundError:
                self.send_error(404)
                return

        # API endpoints
        if self.path == "/api/dashboard/nodes":
            self.send_json(get_nodes_json())
            return
        if self.path == "/api/dashboard/events":
            self.send_json(list(recent))
            return
        if self.path == "/api/dashboard/errors":
            self.send_json({"rejected_total": totals["rejected"], "errors": list(errors)})
            return
        if self.path.startswith("/api/dashboard/history"):
            # Parse minutes parameter
            minutes = 15
            if "?" in self.path:
                query = self.path.split("?", 1)[1]
                for part in query.split("&"):
                    if part.startswith("minutes="):
                        try:
                            minutes = int(part.split("=", 1)[1])
                        except ValueError:
                            pass
            if minutes not in HISTORY_MINUTES:
                self.send_error(400, "minutes must be 5, 15, or 60")
                return
            self.send_json(get_history_json(minutes))
            return

        # Anything else
        self.send_error(404)

    def do_POST(self):
        if self.path != "/api/telemetry":
            self.send_error(404)
            return

        now = time.time()
        source = self.client_address[0]

        # 1. Token check
        token = self.headers.get("X-LUMPI-Token", "")
        if not hmac.compare_digest(token, os.environ.get("LUMPI_LAN_KEY", "")):
            totals["rejected"] += 1
            totals["error_id"] += 1
            err = {
                "id": totals["error_id"],
                "time": now,
                "source": source,
                "reason": "bad token",
                "hint": find_hint("bad token"),
                "raw": "",
                "example": get_example_for_error("bad token"),
            }
            errors.appendleft(err)
            print(f"rejected from {source}: bad token")
            self.send_json({"error": "bad token"}, 401)
            return

        # 2. Content-Length
        cl = self.headers.get("Content-Length")
        if cl is None:
            totals["rejected"] += 1
            totals["error_id"] += 1
            err = {
                "id": totals["error_id"],
                "time": now,
                "source": source,
                "reason": "bad length: got None",
                "hint": find_hint("bad length"),
                "raw": "",
                "example": get_example_for_error("bad length"),
            }
            errors.appendleft(err)
            print(f"rejected from {source}: bad length: got None")
            self.send_json({"error": "bad length: got None"}, 400)
            return
        try:
            length = int(cl)
        except ValueError:
            totals["rejected"] += 1
            totals["error_id"] += 1
            err = {
                "id": totals["error_id"],
                "time": now,
                "source": source,
                "reason": f"bad length: got {cl}",
                "hint": find_hint("bad length"),
                "raw": "",
                "example": get_example_for_error("bad length"),
            }
            errors.appendleft(err)
            print(f"rejected from {source}: bad length: got {cl}")
            self.send_json({"error": f"bad length: got {cl}"}, 400)
            return
        if length < 0 or length > MAX_BODY_BYTES:
            totals["rejected"] += 1
            totals["error_id"] += 1
            err = {
                "id": totals["error_id"],
                "time": now,
                "source": source,
                "reason": f"bad length: got {length}",
                "hint": find_hint("bad length"),
                "raw": "",
                "example": get_example_for_error("bad length"),
            }
            errors.appendleft(err)
            print(f"rejected from {source}: bad length: got {length}")
            self.send_json({"error": f"bad length: got {length}"}, 400)
            return

        # 3. Read body
        raw_body = self.rfile.read(length).decode("utf-8", errors="replace")

        # 4. Parse JSON
        try:
            evt = json.loads(raw_body)
        except json.JSONDecodeError:
            totals["rejected"] += 1
            totals["error_id"] += 1
            err = {
                "id": totals["error_id"],
                "time": now,
                "source": source,
                "reason": "not JSON",
                "hint": find_hint("not JSON"),
                "raw": raw_body[:200],
                "example": get_example_for_error("not JSON"),
            }
            errors.appendleft(err)
            print(f"rejected from {source}: not JSON")
            self.send_json({"error": "not JSON"}, 400)
            return

        # 5. Check event
        error = check_event(evt)
        if error:
            totals["rejected"] += 1
            totals["error_id"] += 1
            err = {
                "id": totals["error_id"],
                "time": now,
                "source": source,
                "reason": error,
                "hint": find_hint(error),
                "raw": raw_body[:200],
                "example": get_example_for_error(error),
            }
            errors.appendleft(err)
            print(f"rejected from {source}: {error}")
            record_message(0, now, source, event=evt, expect="rejected")
            self.send_json({"error": error}, 400)
            return

        # 6. Add event
        result = add_event(evt, now, source)
        record_message(0, now, source, event=evt, expect=result)
        self.send_json({"result": result})

    def send_json(self, data, status=200):
        content = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)

    def log_message(self, format, *args):
        # Suppress default log_message to avoid polluting output
        pass


def get_nodes_json():
    """Build node list for /api/dashboard/nodes."""
    now = time.time()
    nodes = []
    # All nodes that should be shown: SHOWN_NODES union known nodes
    known = set(latest.keys()) | set(SHOWN_NODES)
    for nid in sorted(known):
        if nid not in latest:
            nodes.append({
                "node_id": nid,
                "status": "unknown",
                "counts": {"added": counts.get(nid, {}).get("added", 0), "duplicate": counts.get(nid, {}).get("duplicate", 0)},
            })
            continue
        info = latest[nid]
        age = now - info["received_at"]
        status = node_status(age)
        node_data = {
            "node_id": nid,
            "status": status,
            "age_s": round(age, 1),
            "last_seen": info["received_at"],
            "recorded_at": info["recorded_at"],
            "event_type": info["event_type"],
            "activity": info["activity"],
            "values": info["values"],
            "counts": {"added": counts.get(nid, {}).get("added", 0), "duplicate": counts.get(nid, {}).get("duplicate", 0)},
        }
        if nid == 0:
            lux = info["values"].get("lux")
            node_data["low_light"] = (lux is not None and lux < DARK_LUX)
        nodes.append(node_data)
    return nodes


def get_history_json(minutes):
    """Build history response for /api/dashboard/history?minutes=X."""
    now = time.time()
    cutoff = now - minutes * 60
    nodes_out = {}
    for nid in SHOWN_NODES:
        points = []
        if nid in history:
            for h in history[nid]:
                if h["t"] >= cutoff:
                    points.append({
                        "t": h["t"],
                        "recorded_at": h["recorded_at"],
                        "event_type": h["event_type"],
                        "activity": h["activity"],
                        "data": h["data"],
                    })
        nodes_out[str(nid)] = points
    return {
        "now": now,
        "minutes": minutes,
        "dark_lux": DARK_LUX,
        "stale_after_s": STALE_AFTER_S,
        "nodes": nodes_out,
    }


def main():
    global record_fp

    parser = argparse.ArgumentParser()
    parser.add_argument("--record", help="Append checked messages to file.jsonl")
    args = parser.parse_args()

    # LUMPI_LAN_KEY required
    key = os.environ.get("LUMPI_LAN_KEY")
    if not key:
        print("LUMPI_LAN_KEY environment variable required", file=sys.stderr)
        print("Example: LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py", file=sys.stderr)
        sys.exit(1)

    if args.record:
        record_fp = open(args.record, "a", encoding="utf-8")

    # Line buffering so a log file is complete after a crash
    sys.stdout.reconfigure(line_buffering=True)

    lan_ip = get_lan_ip()
    print(f"dashboard on http://localhost:{PORT}")
    print(f"LUMPI_ADAPTER_URL=http://{lan_ip}:{PORT}")

    server = HTTPServer(("", PORT), DashboardHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if record_fp:
            record_fp.close()


if __name__ == "__main__":
    main()