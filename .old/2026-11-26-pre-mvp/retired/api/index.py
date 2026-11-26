"""LUMPI cloud API: one Vercel Python function. This file holds only the routes and the passwords;
the work is done by the modules in api/_lib/ (read docs/cloud-code-guide.md first).

    POST /api/telemetry          nest: one event (docs/dashboard-api.md)      LAN key
    POST /v1/assess              nest: one recording, answers "code,word"      LAN key
    GET  /v1/page?uid&page       nest or teacher: one page's words             LAN key or dashboard password
    GET  /v1/word_audio?uid&passage&word   one word's practice clip (WAV)      LAN key or dashboard password
    GET  /api/dashboard/{nodes,history,scores}   the dashboard pages           dashboard password

Environment (Vercel project settings, never in git): SUPABASE_URL, SUPABASE_SECRET_KEY,
LUMPI_LAN_KEY, LUMPI_DASHBOARD_PASSWORD, LUMPI_AI_URL, LUMPI_AI_KEY.
Local test (same variables exported): python3 api/index.py   -> http://localhost:8081
"""
import base64
import hmac
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent))  # so "_lib" is found, locally and on Vercel
from _lib import assess, books, events, views  # noqa: E402

os.environ["TZ"] = "Asia/Bangkok"  # Vercel runs in UTC; a Thai class at 07:30 must count as "today"
time.tzset()
MAX_EVENT_BYTES = 8192  # a score event with every word of a page fits easily


def token_ok(headers):
    """The LAN key, sent by the nest as "Authorization: Bearer <key>"."""
    key = os.environ.get("LUMPI_LAN_KEY", "")
    given = headers.get("Authorization", "").removeprefix("Bearer ")
    # Compared as bytes (compare_digest refuses non-ASCII text) and in constant time.
    return bool(key) and hmac.compare_digest(given.encode(), key.encode())


def password_ok(headers):
    """The teacher's password, sent by the browser as "Authorization: Basic base64(name:password)"."""
    password = os.environ.get("LUMPI_DASHBOARD_PASSWORD", "")
    header = headers.get("Authorization", "")
    if not password or not header.startswith("Basic "):
        return False
    try:
        given = base64.b64decode(header[6:]).decode("utf-8").split(":", 1)[-1]
    except ValueError:
        return False
    return hmac.compare_digest(given.encode(), password.encode())


class handler(BaseHTTPRequestHandler):
    def reply(self, code, body, content_type="application/json", headers=None):
        if content_type == "application/json":
            body = json.dumps(body)
        body = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def route(self):
        """(route name, query). vercel.json passes the name as ?route=; locally the path's last part is used."""
        url = urlsplit(self.path)
        query = {name: values[0] for name, values in parse_qs(url.query).items()}
        return query.get("route", url.path.rstrip("/").split("/")[-1]), query

    def body_length(self, limit):
        """Content-Length, or -1 when it is missing or not between 1 and limit."""
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            return -1
        return length if 0 <= length <= limit else -1

    def do_GET(self):
        name, query = self.route()
        if name in ["page", "word_audio"]:
            if not (token_ok(self.headers) or password_ok(self.headers)):
                self.reply(401, {"error": "LAN key or dashboard password needed"})
            elif name == "page":
                self.send_page(query.get("uid", ""), query.get("page", ""))
            else:
                self.send_word_audio(query.get("uid", ""), query.get("passage", ""), query.get("word", ""))
            return
        if not password_ok(self.headers):
            # This header makes the browser show its own password box.
            self.reply(401, {"error": "password needed"}, headers={"WWW-Authenticate": 'Basic realm="LUMPI teacher dashboard"'})
            return
        now = time.time()
        minutes, day = query.get("minutes", "15"), query.get("day", "today")
        try:
            if name == "nodes":
                self.reply(200, views.nodes_view(now))
            elif name == "history" and minutes in ["5", "15", "60", "180"]:
                self.reply(200, views.history_view(now, int(minutes)))
            elif name == "scores" and day in ["today", "past"]:
                self.reply(200, views.scores_view(now, day))
            else:
                self.reply(404, {"error": "not found (history: minutes=5, 15, 60 or 180; scores: day=today or past)"})
        except OSError as error:
            self.reply(502, {"error": f"database not reachable: {error}"})

    def send_page(self, uid, page):
        """GET /v1/page: one page's text and words, each with its IPA and its clip's address."""
        try:
            view = books.page_view(uid, page, books.load_book(uid))
        except (OSError, ValueError, KeyError, TypeError) as error:  # Storage down, or a malformed stored book
            self.reply(502, {"error": f"books not readable: {error}"})
            return
        if view is None:
            self.reply(404, {"error": "unknown book or page (add it with tools/add_book.py)"})
        else:
            self.reply(200, view)

    def send_word_audio(self, uid, passage, index):
        """GET /v1/word_audio: the practice clip of word number <index> of a page."""
        try:
            view = books.page_view(uid, passage, books.load_book(uid))
            words = view["words"] if view else []
            word = words[int(index)]["word"] if index.isdigit() and int(index) < len(words) else None
            # The clip's name comes from the book, never from the query, so no other file can be reached.
            clip = books.load_clip(view["language"], word) if word else None
        except (OSError, ValueError, KeyError, TypeError) as error:
            self.reply(502, f"books not readable: {error}", "text/plain")
            return
        if clip is None:
            self.reply(404, "no such book/passage/word, or its clip was not made yet", "text/plain")
        else:
            self.reply(200, clip, "audio/wav")

    def do_POST(self):
        started = time.monotonic()  # /v1/assess: the AI's deadline counts from here
        name, query = self.route()
        if name not in ["telemetry", "assess"]:
            self.reply(404, {"error": "not found"})
        elif not token_ok(self.headers):
            self.reply(401, {"error": "bad token"})
        elif name == "assess":
            self.post_assess(query, started)
        else:
            self.post_telemetry()

    def post_assess(self, query, started):
        node, attempt, passage, uid = (query.get(key, "") for key in ["node", "attempt", "passage", "uid"])
        length = self.body_length(assess.MAX_RECORDING_BYTES)
        if node not in ["1", "2", "3"] or not books.good_page(passage) or not attempt.isdigit() \
                or not books.UID.fullmatch(uid) or length < 1:
            self.reply(400, "bad request: node 1-3, passage 1-999, attempt, uid (hex) and a recording are needed", "text/plain")
            return
        answer = assess.assess(int(node), int(attempt), int(passage), uid, self.rfile.read(length), started)
        self.reply(200, answer, "text/plain")

    def post_telemetry(self):
        length = self.body_length(MAX_EVENT_BYTES)
        if length < 0:
            self.reply(400, {"error": "bad length"})
            return
        try:
            event = json.loads(self.rfile.read(length))
        except ValueError:  # also bytes that are not UTF-8
            self.reply(400, {"error": "not JSON"})
            return
        error = events.check_event(event)
        if error is not None:
            self.reply(400, {"error": error})
            return
        try:
            self.reply(200, {"result": events.store(event)})
        except OSError as error:
            # 503: the nest keeps the event in its outbox and sends it again later.
            self.reply(503, {"error": f"database not reachable: {error}"})


if __name__ == "__main__":
    HTTPServer(("0.0.0.0", 8081), handler).serve_forever()
