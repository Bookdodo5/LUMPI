"""LUMPI dashboard server.

Receives events from the nest and reading scores from the laptop AI, keeps
the latest state and recent history of each node in memory, saves scores to
data/scores/<day>.jsonl and one room reading a minute to data/room/<day>.jsonl
(one file per class day), keeps rejected messages for developers, and serves
the dashboard page.

Run from the project folder (same key as LUMPI_LAN_KEY in config/private.h):
    LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py
    LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py --record build/lesson.jsonl
Then open http://localhost:8080

--record saves every message the server checks, good and bad, in the same
format as tests/fixtures/dashboard, so laptop/replay_events.py can play a
real lesson again. Wrong-key and oversized messages are not recorded.
"""

import hmac
import json
import os
import socket
import sys
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit


PORT = 8080
# A laptop AI score message carries every word of the passage, so it is bigger than a nest event.
MAX_BODY_BYTES = 8192
MAX_WORDS = 80
STALE_AFTER_S = 30
OFFLINE_AFTER_S = 60
RECENT_SIZE = 20
ERROR_SIZE = 50
SEEN_LIMIT = 1000
VALID_CLOCK_AFTER = 1700000000

# Three hours of messages every 5 s per node (a whole class day). Heartbeats are every 10 s.
HISTORY_SIZE = 2160
HISTORY_MINUTES = [5, 15, 60, 180]
# Room readings saved to disk at most once a minute; enough for light and teacher analytics.
ROOM_SAVE_EVERY_S = 60

# Same value as gateway_settings dark_lux in config/settings.h.
DARK_LUX = 100.0
# Sensor error, +/- lux around DARK_LUX. The v2 chart shades this band.
DARK_LUX_ERROR = 20.0
# v3 gauges. Green = works, amber = inside the +/- margin, red = problem.
# Light: too bright above BRIGHT_LUX (+/- DARK_LUX_ERROR).
BRIGHT_LUX = 1000.0
# Teacher distance: near (green) below TEACHER_NEAR_CM, far (red) above it.
# Margin is the same as room::teacher_margin_cm in config/settings.h.
TEACHER_NEAR_CM = 200.0
TEACHER_MARGIN_CM = 20.0
# Doll pressure (FSR, 0-4095): book on the mat above FSR_ACTIVE (+/- FSR_MARGIN).
FSR_ACTIVE = 1500
FSR_MARGIN = 300

# 0 = nest, 1 and 2 = dolls. The gateway parser allows a third doll.
SHOWN_NODES = [0, 1, 2]
VALID_NODES = [0, 1, 2, 3]

# A doll heartbeat carries the full doll state (null when no book), so old
# book values cannot stay on screen after the book is removed.
# score: from the laptop AI, one per reading attempt (see EXAMPLES["score"]).
# help: the child pressed "need help" on the doll. noise: the doll's sound
# level, sent only while the doll is in Quiet Study.
REQUIRED_DATA = {
    "room": ["lux", "distance_cm"],
    "start": ["uid", "passage", "attempt", "fsr"],
    "status": ["uid", "passage", "attempt", "fsr"],
    "score": ["uid", "passage", "attempt", "score", "words"],
    "help": [],
    "noise": ["db"],
}

# These events do not change the doll's mode or book on the page.
EXTRA_EVENTS = ["score", "help", "noise"]

# A reading session starts when the child switches the doll into reading
# and ends when the doll leaves reading. Same list as READING_ACTIVITIES in shared.js.
READING_ACTIVITIES = ["book_on", "recording", "waiting_ai", "feedback"]

ACTIVITIES = [
    None,
    "idle",
    "book_on",
    "recording",
    "waiting_ai",
    "feedback",
    "quiet_study",
    "stopped",
]

# Correct packets, shown next to an error so the hardware developer can compare.
EXAMPLES = {
    "room": {
        "event_id": "lumpi_1a2b3c4d/42",
        "node_id": 0,
        "event_type": "room",
        "recorded_at": 1794794400,
        "activity": None,
        "data": {"lux": 230.5, "distance_cm": 160.0},
    },
    "start": {
        "event_id": "lumpi_1a2b3c4d/43",
        "node_id": 1,
        "event_type": "start",
        "recorded_at": 1794794401,
        "activity": "book_on",
        "data": {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "fsr": 3100},
    },
    "status": {
        "event_id": "lumpi_1a2b3c4d/44",
        "node_id": 1,
        "event_type": "status",
        "recorded_at": 1794794402,
        "activity": "idle",
        "data": {"uid": None, "passage": None, "attempt": None, "fsr": 0},
    },
    # Laptop AI -> POST /api/telemetry, same X-LUMPI-Token header as the nest.
    # score and every word/phoneme score: 0 to 100. ipa: one IPA sound of that word.
    "score": {
        "event_id": "laptop_ai/17",
        "node_id": 1,
        "event_type": "score",
        "recorded_at": 1794794410,
        "activity": None,
        "data": {
            "uid": "04A1B2C3",
            "passage": 2,
            "attempt": 1,
            "score": 72,
            "words": [
                {"word": "think", "score": 55, "phonemes": [{"ipa": "θ", "score": 30}, {"ipa": "ɪ", "score": 80}, {"ipa": "ŋ", "score": 60}, {"ipa": "k", "score": 50}]},
                {"word": "fish", "score": 90, "phonemes": [{"ipa": "f", "score": 95}, {"ipa": "ɪ", "score": 85}, {"ipa": "ʃ", "score": 90}]},
            ],
        },
    },
    "help": {
        "event_id": "lumpi_1a2b3c4d/45",
        "node_id": 1,
        "event_type": "help",
        "recorded_at": 1794794420,
        "activity": None,
        "data": {},
    },
    "noise": {
        "event_id": "lumpi_1a2b3c4d/46",
        "node_id": 2,
        "event_type": "noise",
        "recorded_at": 1794794430,
        "activity": None,
        "data": {"db": 68.5},
    },
}

# First match wins. Matched against the error text before the ":".
HINTS = [
    ("not JSON", "The body is not valid JSON. Check quotes and commas, and that the whole message was sent, not cut off. A query string like node=1&uid=... must be turned into JSON by the nest first."),
    ("not a JSON object", "Send one JSON object {...}, not a list or a single value."),
    ("missing data.", "This event type needs all of its data fields. Use null for a sensor that failed, or when there is no book."),
    ("missing ", "Every event needs event_id, node_id, event_type, recorded_at, activity and data."),
    ("bad event_id", "event_id must look like <gateway id>/<number>, for example lumpi_1a2b3c4d/42, shorter than 96 characters."),
    ("bad node_id", "node_id must be the number 0 (nest), 1, 2 or 3 (dolls). A number, not text. Check NODE_ID in the doll firmware."),
    ("room event must", "Only the nest (node_id 0) sends room events."),
    ("doll event cannot", "node_id 0 is the nest. A doll must use its own number: 1, 2 or 3."),
    ("bad event_type", "event_type must be start, status, room, score, help or noise."),
    ("bad recorded_at", "recorded_at must be unix seconds as a whole number. The nest needs NTP time before sending."),
    ("bad activity", "activity must be idle, book_on, recording, waiting_ai, feedback, quiet_study, stopped, or null."),
    ("data is not", "data must be an object {...}."),
    ("bad data.score", "score must be a number from 0 to 100."),
    ("bad data.words", f"words must be a list (at most {MAX_WORDS}) of {{word, score, phonemes: [{{ipa, score}}]}}. Every score is 0 to 100."),
    ("bad data.", "Each data value must be a number, text or null. No lists or nested objects."),
    ("bad token", "The X-LUMPI-Token header is missing or wrong. It must match LUMPI_LAN_KEY on the laptop."),
    ("bad length", f"Content-Length is missing, or the body is larger than {MAX_BODY_BYTES} bytes."),
]

# Serve dashboard/v7 by default. LUMPI_DASHBOARD=v1 to v6 serves an older page.
# The v5 developer page is also served under /debug/ (the "Advanced / debug" link).
DASHBOARD_DIR = Path(__file__).resolve().parent.parent / "dashboard"
PAGE_DIR = DASHBOARD_DIR / os.environ.get("LUMPI_DASHBOARD", "v7")
DEBUG_DIR = DASHBOARD_DIR / "v5"
# One file per day. The tests point this at a temporary folder.
SCORES_DIR = Path(__file__).resolve().parent.parent / "data" / "scores"
ROOM_DIR = Path(__file__).resolve().parent.parent / "data" / "room"
PAGE_FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/doll.html": ("doll.html", "text/html; charset=utf-8"),
    "/session.html": ("session.html", "text/html; charset=utf-8"),
    "/style.css": ("style.css", "text/css; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
    "/shared.js": ("shared.js", "text/javascript; charset=utf-8"),
    "/doll.js": ("doll.js", "text/javascript; charset=utf-8"),
    "/session.js": ("session.js", "text/javascript; charset=utf-8"),
    "/charts.js": ("charts.js", "text/javascript; charset=utf-8"),
}

latest = {}
recent = deque(maxlen=RECENT_SIZE)
seen_ids = set()
history = {}
counts = {}
errors = deque(maxlen=ERROR_SIZE)
totals = {"rejected": 0, "error_id": 0}
# node_id -> recorded_at when the doll's current reading session began.
session_start = {}
# recorded_at of the last room reading saved to disk.
room_saved = {"at": 0}
recorder = {"file": None, "last_time": None}
# Requests run in threads (see make_server), so the state above is changed and
# read under this one lock.
state_lock = threading.Lock()


def clear_state():
    latest.clear()
    recent.clear()
    seen_ids.clear()
    history.clear()
    counts.clear()
    errors.clear()
    totals["rejected"] = 0
    totals["error_id"] = 0
    session_start.clear()
    room_saved["at"] = 0


def is_whole_number(value):
    # bool is a subclass of int in Python, so reject it on purpose.
    return isinstance(value, int) and not isinstance(value, bool)


def is_plain_value(value):
    if value is None or isinstance(value, str):
        return True
    if isinstance(value, bool):
        return False
    return isinstance(value, (int, float))


def is_score(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and 0 <= value <= 100


def check_words(words):
    """Return an error message, or None. words = [{word, score, phonemes: [{ipa, score}]}]."""
    if not isinstance(words, list) or len(words) > MAX_WORDS:
        return f"bad data.words: got {shown(words)}"
    for item in words:
        if not isinstance(item, dict) or not isinstance(item.get("word"), str) or not is_score(item.get("score")):
            return f"bad data.words item: got {shown(item)}"
        phonemes = item.get("phonemes")
        if not isinstance(phonemes, list):
            return f"bad data.words phonemes: got {shown(item)}"
        for sound in phonemes:
            if not isinstance(sound, dict) or not isinstance(sound.get("ipa"), str) or not is_score(sound.get("score")):
                return f"bad data.words phoneme: got {shown(sound)}"
    return None


def shown(value):
    """Short JSON text of a value, for error messages."""
    return json.dumps(value)[:60]


def check_event(event):
    """Return an error message, or None when the event is valid."""
    if not isinstance(event, dict):
        return "event is not a JSON object"

    for key in ["event_id", "node_id", "event_type", "recorded_at", "activity", "data"]:
        if key not in event:
            return f"missing {key}"

    event_id = event["event_id"]
    node_id = event["node_id"]
    event_type = event["event_type"]
    recorded_at = event["recorded_at"]
    data = event["data"]

    if not isinstance(event_id, str) or "/" not in event_id or len(event_id) >= 96:
        return f"bad event_id: got {shown(event_id)}"
    if not is_whole_number(node_id) or node_id not in VALID_NODES:
        return f"bad node_id: got {shown(node_id)}"
    if event_type not in REQUIRED_DATA:
        return f"bad event_type: got {shown(event_type)}"
    if event_type == "room" and node_id != 0:
        return f"room event must come from node 0: got node {node_id}"
    if event_type != "room" and node_id == 0:
        return f"doll event cannot use node 0: got {event_type}"
    if not is_whole_number(recorded_at) or recorded_at <= VALID_CLOCK_AFTER:
        return f"bad recorded_at: got {shown(recorded_at)}"
    if event["activity"] not in ACTIVITIES:
        return f"bad activity: got {shown(event['activity'])}"
    if not isinstance(data, dict):
        return f"data is not an object: got {shown(data)}"

    for key in REQUIRED_DATA[event_type]:
        if key not in data:
            return f"missing data.{key}"

    if event_type == "score":
        if not is_score(data["score"]):
            return f"bad data.score: got {shown(data['score'])}"
        error = check_words(data["words"])
        if error is not None:
            return error

    for key, value in data.items():
        if key == "words" and event_type == "score":
            continue
        if not is_plain_value(value):
            return f"bad data.{key}: got {shown(value)}"

    return None


def hint_for(reason):
    head = reason.split(":")[0]
    for text, hint in HINTS:
        if text in head:
            return hint
    return ""


def record_error(now, source, reason, raw, event_type=None):
    totals["rejected"] += 1
    totals["error_id"] += 1

    if event_type not in EXAMPLES:
        event_type = "status"

    errors.appendleft({
        "id": totals["error_id"],
        "time": now,
        "source": source,
        "reason": reason,
        "hint": hint_for(reason),
        "raw": raw[:MAX_BODY_BYTES],
        "example": EXAMPLES[event_type],
    })


def node_counts(node_id):
    if node_id not in counts:
        counts[node_id] = {"added": 0, "duplicate": 0}
    return counts[node_id]


def scores_path(day):
    return SCORES_DIR / f"{day}.jsonl"


def append_line(path, line):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a", encoding="utf-8") as file:
        file.write(json.dumps(line, ensure_ascii=False) + "\n")


def read_lines(path):
    if not path.exists():
        return []
    return [json.loads(text) for text in path.read_text(encoding="utf-8").splitlines() if text.strip()]


def save_room(event):
    """Keep one room reading a minute, so past classes can compare light and teacher distance with scores."""
    if event["recorded_at"] - room_saved["at"] < ROOM_SAVE_EVERY_S:
        return
    room_saved["at"] = event["recorded_at"]
    line = {"recorded_at": event["recorded_at"], **event["data"]}
    append_line(ROOM_DIR / f"{day_of(event['recorded_at'])}.jsonl", line)


def day_of(unix_seconds):
    return time.strftime("%Y-%m-%d", time.localtime(unix_seconds))


def save_score(event):
    """Append one score to that day's file, tagged with the doll's reading session."""
    node_id = event["node_id"]
    # A score with no reading session open (status lost) starts one.
    session = session_start.setdefault(node_id, event["recorded_at"])
    line = {"node_id": node_id, "recorded_at": event["recorded_at"], "session": session, **event["data"]}
    append_line(scores_path(day_of(event["recorded_at"])), line)


def read_scores(day):
    return read_lines(scores_path(day))


def scores_view(now, which):
    """which = "today" or "past". Past = every saved day, today included, for the trend over classes."""
    today = day_of(now)
    if which == "today":
        return {"today": today, "scores": read_scores(today), "room": read_lines(ROOM_DIR / f"{today}.jsonl")}

    # shortcut: reads every day file on each call; fine for one term, add a summary file if it gets slow.
    days = sorted(path.stem for path in SCORES_DIR.glob("*.jsonl"))
    scores = []
    room = []
    for day in days:
        scores.extend(read_scores(day))
        room.extend(read_lines(ROOM_DIR / f"{day}.jsonl"))
    return {"today": today, "days": days, "scores": scores, "room": room}


def add_event(event, now):
    """Save a valid event. Return 'added' or 'duplicate'."""
    event_id = event["event_id"]
    node_id = event["node_id"]

    if event_id in seen_ids:
        node_counts(node_id)["duplicate"] += 1
        return "duplicate"

    # shortcut: forgets old IDs after 1000 events, enough for one lesson; use SQLite in T15.
    if len(seen_ids) >= SEEN_LIMIT:
        seen_ids.clear()
    seen_ids.add(event_id)

    node_counts(node_id)["added"] += 1
    recent.appendleft({**event, "received_at": now})

    # Scores come from the laptop AI, not the doll: saved to disk, not to history.
    if event["event_type"] == "score":
        save_score(event)
        return "added"

    if event["event_type"] == "room":
        save_room(event)

    if node_id not in history:
        history[node_id] = deque(maxlen=HISTORY_SIZE)
    history[node_id].append({
        "t": now,
        "recorded_at": event["recorded_at"],
        "event_type": event["event_type"],
        "activity": event["activity"],
        "data": dict(event["data"]),
    })

    node = latest.get(node_id)
    if node is None:
        node = {"recorded_at": 0, "event_type": None, "activity": None, "values": {}}
        latest[node_id] = node

    # Any valid event proves the node is alive.
    node["received_at"] = now

    if event["event_type"] == "help":
        # Nest time (NTP), so a replayed or late press keeps its real time.
        node["help_at"] = max(node.get("help_at", 0), event["recorded_at"])
        return "added"

    if event["event_type"] == "noise":
        node["noise_db"] = event["data"]["db"]
        node["noise_at"] = event["recorded_at"]
        return "added"

    # A late, older event must not overwrite newer values.
    if event["recorded_at"] >= node["recorded_at"]:
        node["recorded_at"] = event["recorded_at"]
        node["event_type"] = event["event_type"]
        node["activity"] = event["activity"]
        node["values"].update(event["data"])

        if node_id != 0 and event["activity"] in READING_ACTIVITIES:
            session_start.setdefault(node_id, event["recorded_at"])
        elif node_id != 0:
            session_start.pop(node_id, None)

    return "added"


def node_status(age_s):
    if age_s is None:
        return "unknown"
    if age_s < STALE_AFTER_S:
        return "online"
    if age_s < OFFLINE_AFTER_S:
        return "stale"
    return "offline"


def nodes_view(now):
    result = []
    node_ids = sorted(set(SHOWN_NODES) | set(latest))

    for node_id in node_ids:
        node = latest.get(node_id)
        node_count = counts.get(node_id, {"added": 0, "duplicate": 0})

        if node is None:
            result.append({"node_id": node_id, "status": "unknown", "counts": node_count})
            continue

        age_s = now - node["received_at"]
        item = {
            "node_id": node_id,
            "status": node_status(age_s),
            "age_s": round(age_s, 1),
            "last_seen": node["received_at"],
            "recorded_at": node["recorded_at"],
            "event_type": node["event_type"],
            "activity": node["activity"],
            "values": node["values"],
            "counts": node_count,
            "help_at": node.get("help_at"),
            "help_age_s": None if node.get("help_at") is None else round(time.time() - node["help_at"], 1),
            "noise_db": node.get("noise_db"),
            "noise_at": node.get("noise_at"),
        }

        if node_id == 0:
            lux = node["values"].get("lux")
            item["low_light"] = lux is not None and lux < DARK_LUX

        result.append(item)

    return result


def history_view(now, minutes):
    start = now - minutes * 60
    nodes = {}

    for node_id in sorted(set(SHOWN_NODES) | set(history)):
        points = history.get(node_id, [])
        # Both times: a message replayed from an earlier day arrives now but belongs to that day.
        nodes[str(node_id)] = [point for point in points if point["t"] >= start and point["recorded_at"] >= start]

    return {
        "now": now,
        "minutes": minutes,
        "dark_lux": DARK_LUX,
        "dark_lux_error": DARK_LUX_ERROR,
        "bright_lux": BRIGHT_LUX,
        "teacher_near_cm": TEACHER_NEAR_CM,
        "teacher_margin_cm": TEACHER_MARGIN_CM,
        "fsr_active": FSR_ACTIVE,
        "fsr_margin": FSR_MARGIN,
        "stale_after_s": STALE_AFTER_S,
        "nodes": nodes,
    }


def errors_view():
    return {"rejected_total": totals["rejected"], "errors": list(errors)}


def record_message(now, source, body_text, event, result):
    """Append one line to the --record file, in the scenario file format."""
    if recorder["file"] is None:
        return

    delay = 0.0
    if recorder["last_time"] is not None:
        delay = now - recorder["last_time"]
    recorder["last_time"] = now

    line = {"delay": round(delay, 3), "received_at": round(now, 3), "source": source}
    if event is None:
        line["raw"] = body_text
    else:
        line["event"] = event
    line["expect"] = result

    recorder["file"].write(json.dumps(line) + "\n")
    # Flush every line, so a crash or power cut keeps the lesson so far.
    recorder["file"].flush()


def handle_telemetry(body_text, now, source="local"):
    """Check, save and record one event. Return (HTTP code, message)."""
    try:
        event = json.loads(body_text)
    except (json.JSONDecodeError, TypeError):
        record_error(now, source, "not JSON", body_text)
        record_message(now, source, body_text, None, "rejected")
        return 400, "not JSON"

    error = check_event(event)
    if error is not None:
        event_type = None
        if isinstance(event, dict):
            event_type = event.get("event_type")
        record_error(now, source, error, body_text, event_type)
        record_message(now, source, body_text, None, "rejected")
        return 400, error

    result = add_event(event, now)
    record_message(now, source, body_text, event, result)
    return 200, result


class DashboardHandler(BaseHTTPRequestHandler):
    def send_json(self, code, value):
        body = json.dumps(value).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlsplit(self.path)

        # shortcut: the reply is written while holding the lock; fine for small LAN replies.
        if url.path == "/api/dashboard/nodes":
            with state_lock:
                self.send_json(200, nodes_view(time.time()))
            return

        if url.path == "/api/dashboard/events":
            with state_lock:
                self.send_json(200, list(recent))
            return

        if url.path == "/api/dashboard/errors":
            with state_lock:
                self.send_json(200, errors_view())
            return

        if url.path == "/api/dashboard/history":
            text = parse_qs(url.query).get("minutes", ["15"])[0]
            if not text.isdigit() or int(text) not in HISTORY_MINUTES:
                self.send_json(400, {"error": f"minutes must be one of {HISTORY_MINUTES}"})
                return
            with state_lock:
                self.send_json(200, history_view(time.time(), int(text)))
            return

        if url.path == "/api/dashboard/scores":
            which = parse_qs(url.query).get("day", ["today"])[0]
            if which not in ["today", "past"]:
                self.send_json(400, {"error": "day must be today or past"})
                return
            with state_lock:
                self.send_json(200, scores_view(time.time(), which))
            return

        page_dir = PAGE_DIR
        page_path = url.path
        if page_path.startswith("/debug/"):
            page_dir = DEBUG_DIR
            page_path = page_path[len("/debug"):]

        if page_path not in PAGE_FILES:
            self.send_json(404, {"error": "not found"})
            return

        file_name, content_type = PAGE_FILES[page_path]
        # Not every version has every file (v6 has no charts.js, v5 has no doll.html).
        if not (page_dir / file_name).exists():
            self.send_json(404, {"error": "not found"})
            return
        body = (page_dir / file_name).read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/api/telemetry":
            self.send_json(404, {"error": "not found"})
            return

        source = self.client_address[0]

        token = self.headers.get("X-LUMPI-Token", "")
        if not hmac.compare_digest(token.encode("utf-8"), self.server.token.encode("utf-8")):
            # The body is not kept, so a stranger cannot put text on the page.
            print(f"rejected from {source}: bad token")
            with state_lock:
                record_error(time.time(), source, "bad token", "")
            self.send_json(401, {"error": "bad token"})
            return

        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            length = -1

        if length < 0 or length > MAX_BODY_BYTES:
            print(f"rejected from {source}: bad length {length}")
            with state_lock:
                record_error(time.time(), source, f"bad length: got {length}", "")
            self.send_json(400, {"error": "bad length"})
            return

        body_text = self.rfile.read(length).decode("utf-8", errors="replace")
        with state_lock:
            code, message = handle_telemetry(body_text, time.time(), source)

        if code != 200:
            print(f"rejected from {source}: {message}")
            self.send_json(code, {"error": message})
            return

        self.send_json(200, {"result": message})

    def log_message(self, format, *args):
        # The page polls every second; printing every request hides real problems.
        pass


def make_server(port, token):
    # Threading: a browser often opens a spare connection and sends nothing on it.
    # With the plain HTTPServer that one connection blocked every other request.
    server = ThreadingHTTPServer(("0.0.0.0", port), DashboardHandler)
    server.token = token
    return server


def lan_address():
    """This laptop's address on the local network, for LUMPI_ADAPTER_URL."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # connect() on UDP sends nothing; it only picks the network card to use.
        probe.connect(("10.255.255.255", 1))
        return probe.getsockname()[0]
    except OSError:
        return "localhost"
    finally:
        probe.close()


def main():
    # Write each log line at once, so a log file is complete even after a crash.
    sys.stdout.reconfigure(line_buffering=True)

    token = os.environ.get("LUMPI_LAN_KEY", "")
    if not token:
        print("Set LUMPI_LAN_KEY first (same value as in config/private.h).")
        return

    record_path = None
    if len(sys.argv) == 3 and sys.argv[1] == "--record":
        record_path = Path(sys.argv[2])
    elif len(sys.argv) != 1:
        print("usage: python3 laptop/dashboard_server.py [--record file.jsonl]")
        return

    server = make_server(PORT, token)

    if record_path is not None:
        record_path.parent.mkdir(parents=True, exist_ok=True)
        # Append, so restarting the server does not wipe an earlier recording.
        recorder["file"] = open(record_path, "a", encoding="utf-8")
        print(f"recording to {record_path}")

    print(f"dashboard on http://localhost:{PORT}")
    print(f'firmware: set LUMPI_ADAPTER_URL to "http://{lan_address()}:{PORT}" in config/private.h')

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("stopped")
    finally:
        if recorder["file"] is not None:
            recorder["file"].close()


if __name__ == "__main__":
    main()
