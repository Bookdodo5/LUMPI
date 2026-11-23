"""LUMPI cloud API (Vercel Python function). Same routes as laptop/dashboard_server.py.

The data lives in Supabase (supabase/migrations/), not in memory: every event is one
row, stored once by event_id. This file only checks, stores and reads. The checks and
the page views are the laptop server's own functions, imported, so both stay the same.

    POST /api/telemetry          nest, mock_class.py              LAN key (see token_ok)
    POST /v1/assess              nest, with the recording         LAN key; answers "code,word"
    GET  /api/dashboard/<view>   the dashboard pages              browser password (LUMPI_DASHBOARD_PASSWORD)

Environment (Vercel project settings, never in git):
    SUPABASE_URL, SUPABASE_SECRET_KEY, LUMPI_LAN_KEY, LUMPI_DASHBOARD_PASSWORD
    LUMPI_AI_URL, LUMPI_AI_KEY: the AI's Runpod "runsync" address and API key (tests: tests/mock_ai.py).
    LUMPI_TZ (optional, default Asia/Bangkok): which clock decides "today".

Run locally for a test (same variables exported), API only, port 8081:
    python3 api/index.py
"""
import base64
import gzip
import hmac
import json
import os
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "laptop"))
import dashboard_server as ds  # noqa: E402

# Vercel runs in UTC. A Thai class at 07:30 must still count as "today".
os.environ["TZ"] = os.environ.get("LUMPI_TZ", "Asia/Bangkok")
time.tzset()

# The cloud keeps scores and room readings in the database, not in day files.
ds.save_score = lambda event: None
ds.save_room = lambda event: None

FIELDS = ["event_id", "node_id", "event_type", "recorded_at", "activity", "data"]
PAGE_ROWS = 1000  # Supabase returns at most 1000 rows per request.
NODES_WINDOW_S = 300  # help and noise popups last 5 minutes
# The nest waits 12 s for /v1/assess (guide: integration/ai). Answer "uncertain" before that.
AI_TIMEOUT_S = 10
MAX_RECORDING_BYTES = 256000  # 8 s of 16 kHz 16-bit mono, same as Session::max_bytes
POOL = ThreadPoolExecutor(max_workers=4)  # runs database lookups alongside slower work


def rest(method, path, body=None, headers=None):
    """One Supabase REST call with the secret key. Returns the decoded JSON (or None)."""
    key = os.environ["SUPABASE_SECRET_KEY"]
    all_headers = {"apikey": key, "Content-Type": "application/json", "Accept-Encoding": "gzip"}
    all_headers.update(headers or {})
    data = None if body is None else json.dumps(body).encode("utf-8")
    request = urllib.request.Request(os.environ["SUPABASE_URL"] + "/rest/v1/" + path, data=data, method=method, headers=all_headers)
    with urllib.request.urlopen(request, timeout=20) as reply:
        raw = reply.read()
        # gzip makes the reply about 5-10x smaller; Supabase's free plan limits download bytes.
        if reply.headers.get("Content-Encoding") == "gzip":
            raw = gzip.decompress(raw)
    return json.loads(raw) if raw else None


def select_all(query):
    """GET every row of a query, 1000 at a time."""
    rows = []
    while True:
        page = rest("GET", f"{query}&limit={PAGE_ROWS}&offset={len(rows)}")
        rows.extend(page)
        if len(page) < PAGE_ROWS:
            return rows


def as_event(row):
    return {name: row[name] for name in FIELDS}


def session_start(rows, score_time):
    """rows = this doll's start/status events before the score, newest first.
    The session began at the oldest reading event of the unbroken run of reading events.
    Same rule as add_event() + save_score() in the laptop server."""
    start = None
    for row in rows:
        if row["activity"] not in ds.READING_ACTIVITIES:
            break
        start = row["recorded_at"]
    return start if start is not None else score_time


def reject(source, reason, raw, event_type=None):
    try:
        rest("POST", "rejects", [{"source": source, "reason": reason, "raw": raw[: ds.MAX_BODY_BYTES], "event_type": event_type}],
             {"Prefer": "return=minimal"})
    except urllib.error.URLError:
        pass  # a lost debug note must not hide the real 400 answer


def session_rows(node_id, before):
    """This doll's start/status events up to a time, newest first (input of session_start)."""
    return rest("GET", f"events?select=recorded_at,activity&node_id=eq.{node_id}"
                       f"&event_type=in.(start,status)&recorded_at=lte.{before}"
                       f"&order=recorded_at.desc&limit=500")


def ingest(body_text, source, rows_for_session=None):
    """Check and store one event. Returns (HTTP code, message), like handle_telemetry().
    rows_for_session: already fetched session_rows(), so a score needs no extra lookup."""
    try:
        event = json.loads(body_text)
    except (json.JSONDecodeError, TypeError):
        reject(source, "not JSON", body_text)
        return 400, "not JSON"

    error = ds.check_event(event)
    if error is not None:
        reject(source, error, body_text, event.get("event_type") if isinstance(event, dict) else None)
        return 400, error

    row = as_event(event)
    if event["event_type"] == "score":
        if rows_for_session is None:
            rows_for_session = session_rows(event["node_id"], event["recorded_at"])
        row["session"] = session_start(rows_for_session, event["recorded_at"])

    # ponytail: a resend with the same id but different content is silently ignored,
    # not rejected like adapter/cloud_worker.py did; add a compare if a device ever reuses ids.
    stored = rest("POST", "events?on_conflict=event_id", [row],
                  {"Prefer": "resolution=ignore-duplicates,return=representation"})
    return 200, "added" if stored else "duplicate"


def ask_ai(audio, uid, passage, attempt):
    """Send one recording to the AI (Runpod serverless "runsync" format). Returns its output dict."""
    body = {"input": {"audio_b64": base64.b64encode(audio).decode("ascii"), "uid": uid, "passage": passage, "attempt": attempt}}
    request = urllib.request.Request(os.environ["LUMPI_AI_URL"], data=json.dumps(body).encode("utf-8"), method="POST",
                                     headers={"Authorization": "Bearer " + os.environ.get("LUMPI_AI_KEY", ""),
                                              "Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=AI_TIMEOUT_S) as reply:
        answer = json.loads(reply.read())
    if answer.get("status") != "COMPLETED":
        raise ValueError(f"AI status {answer.get('status')}")
    return answer["output"]


def check_assess(query, length):
    """The nest's /v1/assess question. Returns (node, attempt, passage, uid) or raises ValueError."""
    def one(name):
        return query.get(name, [""])[0]
    node, attempt, passage, uid = one("node"), one("attempt"), one("passage"), one("uid")
    if node not in ["1", "2", "3"] or passage not in ["1", "2", "3"] or not attempt.isdigit():
        raise ValueError("bad node, passage or attempt")
    if not uid or len(uid) > 20 or any(c not in "0123456789ABCDEF" for c in uid):
        raise ValueError("bad uid")
    if not 1 <= length <= MAX_RECORDING_BYTES:
        raise ValueError(f"bad length: got {length}")
    return int(node), int(attempt), int(passage), uid


def assess(node, attempt, passage, uid, audio):
    """Grade one recording, store the score event, return "code,word" for the nest."""
    # Look up the reading session WHILE the AI works, so the database adds no waiting time.
    lookup = POOL.submit(session_rows, node, int(time.time()))
    try:
        output = ask_ai(audio, uid, passage, attempt)
        code, word = int(output["code"]), int(output["word"])
        if code not in [1, 2, 3, 4] or not 0 <= word <= 2:
            raise ValueError(f"AI answered {code},{word}")
    except (urllib.error.URLError, TimeoutError, ValueError, KeyError, TypeError) as error:
        # Guide rule: always answer; an AI failure is "uncertain" and the doll asks to try again.
        reject("ai", f"AI failed: {error}", "", "score")
        return "4,0"

    now = time.time()
    score = {"event_id": f"cloud_ai/{node}-{attempt}-{int(now * 1000)}", "node_id": node, "event_type": "score",
             "recorded_at": int(now), "activity": None,
             "data": {"uid": uid, "passage": passage, "attempt": attempt, "score": output.get("score"), "words": output.get("words")}}
    # The score goes through the same checks as every other sender. A bad score is
    # only logged (rejects); the doll still gets its answer.
    ingest(json.dumps(score), "ai", lookup.result())
    return f"{code},{word}"


def replay(rows):
    """Rebuild the laptop server's in-memory state from stored rows, oldest first."""
    ds.clear_state()
    for row in sorted(rows, key=lambda row: row["received_at"]):
        ds.add_event(as_event(row), row["received_at"])


def nodes_view(now):
    rows = select_all(f"events?select=*&received_at=gte.{now - NODES_WINDOW_S}&order=received_at")
    seen = {row["node_id"] for row in rows}
    # A doll silent for longer than the window still shows its last state as "offline".
    # The lookups run at the same time, not one after another.
    missing = [node_id for node_id in ds.SHOWN_NODES if node_id not in seen]
    for found in POOL.map(lambda node_id: rest("GET", f"events?select=*&node_id=eq.{node_id}&order=received_at.desc&limit=1"), missing):
        rows += found
    replay(rows)
    return ds.nodes_view(now)


def history_view(now, minutes):
    # ponytail: replays the whole window on every poll (about 1100 rows an hour, gzip);
    # if Supabase download bytes run short, keep a per-minute summary table instead.
    replay(select_all(f"events?select=*&received_at=gte.{now - minutes * 60}&order=received_at"))
    return ds.history_view(now, minutes)


def scores_view(now, which):
    today = ds.day_of(now)
    since = 0
    if which == "today":
        since = int(time.mktime(time.strptime(today, "%Y-%m-%d")))

    score_rows = select_all(f"events?select=node_id,recorded_at,session,data&event_type=eq.score&recorded_at=gte.{since}&order=recorded_at")
    room_rows = select_all(f"events?select=recorded_at,data&event_type=eq.room&recorded_at=gte.{since}&order=recorded_at")

    scores = [{"node_id": row["node_id"], "recorded_at": row["recorded_at"], "session": row["session"], **row["data"]} for row in score_rows]
    room = []
    last = 0
    for row in room_rows:
        # One room reading a minute, like save_room().
        if row["recorded_at"] - last >= ds.ROOM_SAVE_EVERY_S:
            last = row["recorded_at"]
            room.append({"recorded_at": row["recorded_at"], **row["data"]})

    if which == "today":
        return {"today": today, "scores": scores, "room": room}
    days = sorted({ds.day_of(line["recorded_at"]) for line in scores})
    return {"today": today, "days": days, "scores": scores, "room": room}


def recent_view():
    rows = rest("GET", f"events?select=*&order=received_at.desc&limit={ds.RECENT_SIZE}")
    return [{**as_event(row), "received_at": row["received_at"]} for row in rows]


def errors_view():
    rows = rest("GET", f"rejects?select=*&order=received_at.desc&limit={ds.ERROR_SIZE}")
    ds.clear_state()
    for row in reversed(rows):
        ds.record_error(row["received_at"], row["source"], row["reason"], row["raw"] or "", row["event_type"])
    # ponytail: rejected_total counts only the last 50; use a count query if the page needs the true total.
    return ds.errors_view()


def token_ok(headers):
    """The LAN key: X-LUMPI-Token (dashboard senders) or Authorization: Bearer (messages.h, the guide)."""
    token = os.environ.get("LUMPI_LAN_KEY", "")
    given = headers.get("X-LUMPI-Token") or headers.get("Authorization", "").removeprefix("Bearer ")
    return bool(token) and hmac.compare_digest(given, token)


def password_ok(header):
    password = os.environ.get("LUMPI_DASHBOARD_PASSWORD", "")
    if not password or not header.startswith("Basic "):
        return False
    try:
        given = base64.b64decode(header[6:]).decode("utf-8").split(":", 1)[-1]
    except ValueError:
        return False
    return hmac.compare_digest(given, password)


class handler(BaseHTTPRequestHandler):
    def send_json(self, code, value, extra_headers=None):
        body = json.dumps(value).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        for name, text in (extra_headers or {}).items():
            self.send_header(name, text)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def route(self):
        url = urlsplit(self.path)
        query = parse_qs(url.query)
        # vercel.json passes the page's route as ?route=; locally the path itself is used.
        name = query.get("route", [url.path.rstrip("/").split("/")[-1]])[0]
        return name, query

    def do_GET(self):
        name, query = self.route()
        if not password_ok(self.headers.get("Authorization", "")):
            # The browser shows its own login box for this answer, then remembers it.
            self.send_json(401, {"error": "password needed"}, {"WWW-Authenticate": 'Basic realm="LUMPI teacher dashboard"'})
            return

        now = time.time()
        try:
            if name == "nodes":
                self.send_json(200, nodes_view(now))
            elif name == "events":
                self.send_json(200, recent_view())
            elif name == "errors":
                self.send_json(200, errors_view())
            elif name == "history":
                text = query.get("minutes", ["15"])[0]
                if not text.isdigit() or int(text) not in ds.HISTORY_MINUTES:
                    self.send_json(400, {"error": f"minutes must be one of {ds.HISTORY_MINUTES}"})
                    return
                self.send_json(200, history_view(now, int(text)))
            elif name == "scores":
                which = query.get("day", ["today"])[0]
                if which not in ["today", "past"]:
                    self.send_json(400, {"error": "day must be today or past"})
                    return
                self.send_json(200, scores_view(now, which))
            else:
                self.send_json(404, {"error": "not found"})
        except urllib.error.URLError as error:
            self.send_json(502, {"error": f"database not reachable: {error}"})

    def send_text(self, code, text):
        body = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        name, query = self.route()
        if name not in ["telemetry", "assess"]:
            self.send_json(404, {"error": "not found"})
            return

        source = self.headers.get("X-Forwarded-For", self.client_address[0])
        if not token_ok(self.headers):
            reject(source, "bad token", "")
            self.send_json(401, {"error": "bad token"})
            return

        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            length = -1

        if name == "assess":
            try:
                node, attempt, passage, uid = check_assess(query, length)
            except ValueError as error:
                self.send_text(400, f"bad request: {error}")
                return
            try:
                self.send_text(200, assess(node, attempt, passage, uid, self.rfile.read(length)))
            except urllib.error.URLError:
                self.send_text(200, "4,0")  # database down: the doll still gets an answer
            return

        if length < 0 or length > ds.MAX_BODY_BYTES:
            reject(source, f"bad length: got {length}", "")
            self.send_json(400, {"error": "bad length"})
            return

        body_text = self.rfile.read(length).decode("utf-8", errors="replace")
        try:
            code, message = ingest(body_text, source)
        except urllib.error.URLError as error:
            # 503: the nest keeps the event in its outbox and sends it again later.
            self.send_json(503, {"error": f"database not reachable: {error}"})
            return
        self.send_json(code, {"result": message} if code == 200 else {"error": message})


if __name__ == "__main__":
    print("LUMPI cloud API on http://localhost:8081 (API only)")
    HTTPServer(("0.0.0.0", 8081), handler).serve_forever()
