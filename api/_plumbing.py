"""Plumbing for api/lumpi.py. You do not need to read this to understand LUMPI.

It provides: Handler (the web server: passwords, size limits, routes, replies, the "4,0" safety
answer), db_get/db_insert (Supabase table "events"), load_book/load_clip/save_book/save_clip
(Supabase Storage), ask_ai (the Runpod AI) and the few input checks.
The leading "_" stops Vercel turning this file into a web address of its own.
"""
import base64
import gzip
import hmac
import json
import os
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler
from urllib.parse import parse_qs, urlsplit

os.environ["TZ"] = "Asia/Bangkok"  # Vercel runs in UTC; a Thai class at 07:30 must count as "today"
time.tzset()
UID = re.compile(r"[0-9A-F]{8,20}")       # a book's tag UID: checked before it becomes a file name
WORD = re.compile(r"[a-z]+(?:'[a-z]+)*")  # a clip's word: no "/" or "..", so no other file can be reached
EVENT_TYPES = ["room", "start", "status", "score", "help", "noise"]
FIELDS = ["event_id", "node_id", "event_type", "recorded_at", "activity", "data"]
MAX_EVENT_BYTES = 8192
MAX_RECORDING_BYTES = 256000  # 8 s of 16 kHz 16-bit mono
NEST_WAITS_S = 11             # /v1/assess must answer within the nest's 12 s
deadline = {"at": None}       # set while a /v1/assess request runs


def time_left(keep=0.0):
    """Timeout for the next outside call: short while the nest waits, else 20 s."""
    if deadline["at"] is None:
        return 20
    return max(0.5, deadline["at"] - time.monotonic() - keep)


def supabase(method, path, body=None, headers=None):
    """One call to Supabase with the secret key. Returns the reply bytes, or None if not found (404)."""
    key = os.environ["SUPABASE_SECRET_KEY"]
    all_headers = {"apikey": key, "Authorization": "Bearer " + key, "Content-Type": "application/json",
                   **(headers or {})}
    if path.startswith("/rest/"):  # compressed database replies: the free plan limits download size
        all_headers["Accept-Encoding"] = "gzip"
    request = urllib.request.Request(os.environ["SUPABASE_URL"] + path, data=body, method=method, headers=all_headers)
    try:
        with urllib.request.urlopen(request, timeout=time_left()) as reply:
            raw = reply.read()
            return gzip.decompress(raw) if reply.headers.get("Content-Encoding") == "gzip" else raw
    except urllib.error.HTTPError as error:
        if error.code in (400, 404) and b'"404"' in error.read():  # Storage's "no such file"
            return None
        raise


def db_get(query, limit=None):
    """Rows of a GET query on the database. Without a limit: every row, 1000 at a time."""
    if limit is not None:
        return json.loads(supabase("GET", f"/rest/v1/{query}&limit={limit}"))
    rows = []
    while True:
        page = json.loads(supabase("GET", f"/rest/v1/{query}&limit=1000&offset={len(rows)}"))
        rows += page
        if len(page) < 1000:
            return rows


def db_insert(event):
    """Store one event once. Returns "added", or "duplicate" if its event_id is already stored."""
    row = {key: event[key] for key in FIELDS + ["session"] if key in event}
    reply = supabase("POST", "/rest/v1/events?on_conflict=event_id", json.dumps([row]).encode(),
                     {"Prefer": "resolution=ignore-duplicates,return=representation"})
    return "added" if json.loads(reply) else "duplicate"


def storage_path(name):
    return "/storage/v1/object/books/" + urllib.parse.quote(name)


def load_book(uid):
    """{"title", "language", "passages": {"1": {"text", "words": [{"word", "ipa"}]}}}, or None."""
    raw = supabase("GET", storage_path(f"{uid}.json")) if UID.fullmatch(uid) else None
    return None if raw is None else json.loads(raw)


def load_clip(language, word):
    return supabase("GET", storage_path(f"words/{language}/{word}.wav")) if WORD.fullmatch(word) else None


def save_book(uid, book):
    body = json.dumps(book, ensure_ascii=False, indent=1).encode("utf-8")
    supabase("POST", storage_path(f"{uid}.json"), body, {"x-upsert": "true"})


def save_clip(language, word, wav_bytes):
    supabase("POST", storage_path(f"words/{language}/{word}.wav"), wav_bytes,
             {"x-upsert": "true", "Content-Type": "audio/wav"})


def make_bucket():
    """Create the private "books" bucket the first time."""
    if supabase("GET", "/storage/v1/bucket/books") is None:
        supabase("POST", "/storage/v1/bucket", json.dumps({"id": "books", "name": "books", "public": False}).encode())


def ask_ai(audio):
    """Send a recording to the AI on Runpod ("runsync"). Returns the list of sounds it heard."""
    body = json.dumps({"input": {"audio_b64": base64.b64encode(audio).decode("ascii")}}).encode()
    request = urllib.request.Request(os.environ["LUMPI_AI_URL"], data=body, method="POST",
                                     headers={"Authorization": "Bearer " + os.environ.get("LUMPI_AI_KEY", ""),
                                              "Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=time_left(keep=2)) as reply:  # 2 s kept to store the score
        phones = json.loads(reply.read())["output"]["phones"]
    if not isinstance(phones, list):  # never grade a broken answer: "4,0" instead
        raise ValueError(f"AI answered {phones!r:.60}")
    return phones


def send_discord(text):
    """Post one message to the teacher's Discord channel. Never raises: an alert must not fail the request.
    The webhook URL is a secret: it comes from LUMPI_DISCORD_WEBHOOK and is never printed."""
    webhook = os.environ.get("LUMPI_DISCORD_WEBHOOK")
    if not webhook:
        return
    body = json.dumps({"content": text, "allowed_mentions": {"parse": []}}).encode()
    request = urllib.request.Request(webhook, data=body, method="POST",
                                     headers={"Content-Type": "application/json", "User-Agent": "LUMPI-cloud"})  # Discord refuses urllib's default
    try:
        urllib.request.urlopen(request, timeout=3).close()
    except Exception as error:
        if isinstance(error, urllib.error.HTTPError):
            error.close()
        print(f"discord alert not sent:{type(error).__name__}: {str(error).replace(webhook, '<webhook>')}")


def event_ok(event):
    """Only what stops one bad event from breaking storage or the dashboard."""
    return (isinstance(event, dict) and all(key in event for key in FIELDS)
            and isinstance(event["event_id"], str) and 0 < len(event["event_id"]) < 96
            and type(event["node_id"]) is int and 0 <= event["node_id"] <= 3
            and event["event_type"] in EVENT_TYPES and type(event["recorded_at"]) is int
            and isinstance(event["data"], dict)
            and (event["event_type"] != "score" or isinstance(event["data"].get("words"), list)))


def same(given, secret):
    return bool(secret) and hmac.compare_digest(given.encode(), secret.encode())  # constant-time compare


class Handler(BaseHTTPRequestHandler):
    routes = {}  # filled in by api/lumpi.py

    def lan_key_ok(self):  # the nest sends "Authorization: Bearer <LUMPI_LAN_KEY>"
        return same(self.headers.get("Authorization", "").removeprefix("Bearer "), os.environ.get("LUMPI_LAN_KEY", ""))

    def password_ok(self):  # the browser sends "Authorization: Basic base64(name:password)"
        try:
            given = base64.b64decode(self.headers.get("Authorization", "").removeprefix("Basic ")).decode()
        except ValueError:
            return False
        return same(given.split(":", 1)[-1], os.environ.get("LUMPI_DASHBOARD_PASSWORD", ""))

    def reply(self, code, body, content_type="application/json", headers=None):
        if content_type == "application/json":
            body = json.dumps(body)
        body = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        for name, value in {"Content-Type": content_type, "Cache-Control": "no-store", **(headers or {}),
                            "Content-Length": str(len(body))}.items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def read_body(self, limit):
        """The request body, or None when its size is missing or over the limit."""
        size = self.headers.get("Content-Length", "")
        return self.rfile.read(int(size)) if size.isdigit() and int(size) <= limit else None

    def do_POST(self):
        started = time.monotonic()
        url = urlsplit(self.path)
        q = {name: values[0] for name, values in parse_qs(url.query).items()}
        name = q.get("route", url.path.split("/")[-1])  # vercel.json passes ?route=; locally the path
        if not self.lan_key_ok():
            return self.reply(401, {"error": "bad LAN key"})
        if name == "assess":
            body = self.read_body(MAX_RECORDING_BYTES)
            node, attempt, page, uid = (q.get(key, "") for key in ["node", "attempt", "passage", "uid"])
            if not body or node not in ["1", "2", "3"] or not (attempt + page).isdigit() or not UID.fullmatch(uid):
                return self.reply(400, "bad request", "text/plain")
            deadline["at"] = started + NEST_WAITS_S
            try:
                answer = self.routes["assess"](int(node), int(attempt), int(page), uid, body)
            except Exception as error:  # unknown book, AI or database down or too slow: "try again"
                print(f"assess answered 4,0: {error!r}")
                answer = "4,0"
            deadline["at"] = None
            return self.reply(200, answer, "text/plain")
        if name != "telemetry":
            return self.reply(404, {"error": "not found"})
        body = self.read_body(MAX_EVENT_BYTES)
        try:
            event = json.loads(body)
        except (TypeError, ValueError):
            return self.reply(400, {"error": "not JSON, or too big"})
        if not event_ok(event):
            return self.reply(400, {"error": "bad event (see docs/dashboard-api.md)"})
        try:
            return self.reply(200, {"result": self.routes["telemetry"](event)})
        except OSError as error:  # 503 = database down, the nest resends; 400 = it refused the event, drop it
            status = 400 if getattr(error, "code", None) == 400 else 503
            return self.reply(status, {"error": f"not stored: {error}"})

    def do_GET(self):
        url = urlsplit(self.path)
        q = {name: values[0] for name, values in parse_qs(url.query).items()}
        name = q.get("route", url.path.split("/")[-1])
        try:
            if name in ["page", "word_audio"] and (self.lan_key_ok() or self.password_ok()):
                if name == "page":
                    return self.reply(200, self.routes["page"](q.get("uid", ""), q.get("page", "")))
                clip = self.routes["word_audio"](q.get("uid", ""), q.get("passage", ""), int(q.get("word", "")))
                return self.reply(200, clip, "audio/wav") if clip else self.reply(404, "no clip", "text/plain")
            if name in ["page", "word_audio"]:
                return self.reply(401, {"error": "LAN key or dashboard password needed"})
            if not self.password_ok():  # this header makes the browser ask for the password
                return self.reply(401, {"error": "password needed"},
                                  headers={"WWW-Authenticate": 'Basic realm="LUMPI teacher dashboard"'})
            now = time.time()
            if name == "nodes":
                return self.reply(200, self.routes["nodes"](now))
            if name == "history" and q.get("minutes") in ["60", "180"]:
                return self.reply(200, self.routes["history"](now, int(q["minutes"])))
            if name == "scores" and q.get("day", "today") in ["today", "past"]:
                return self.reply(200, self.routes["scores"](now, q.get("day", "today")))
            return self.reply(404, {"error": "not found"})
        except OSError as error:
            return self.reply(502, {"error": f"database not reachable: {error}"})
        except (KeyError, IndexError, TypeError, ValueError):  # unknown book, page or word
            return self.reply(404, {"error": "no such book, page or word"})
