"""Offline test of the cloud API (api/lumpi.py + api/_plumbing.py): Supabase and the AI are
faked in memory, so it needs no network and no account.

    python3 -m unittest tests.test_cloud
"""
import base64
import json
import os
import sys
import threading
import time
import unittest
import urllib.error
import urllib.request
from http.server import HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "api"))
import _plumbing  # noqa: E402
import lumpi  # noqa: E402

ROWS = []  # the fake "events" table
BOOK = {"title": "Fish", "language": "en-gb", "passages": {"1": {"text": "The fish swims.", "words": [
    {"word": "the", "ipa": "ð ə"}, {"word": "fish", "ipa": "f ɪ ʃ"}, {"word": "swims", "ipa": "s w ɪ m z"}]}}}
FILES = {"/storage/v1/object/books/FFFFFFFF.json": json.dumps(BOOK).encode(),
         "/storage/v1/object/books/words/en-gb/fish.wav": b"RIFF"}
RIGHT = ["ð", "ə", "f", "ɪ", "ʃ", "s", "w", "ɪ", "m", "z"]  # what the AI hears when all is read right
HEARD = RIGHT


def fake_supabase(method, path, body=None, headers=None):
    """Storage = FILES. Database: understands only the inserts and filters the code sends."""
    if path.startswith("/storage/"):
        return FILES.get(path)
    if method == "POST":
        new = [row for row in json.loads(body) if all(old["event_id"] != row["event_id"] for old in ROWS)]
        ROWS.extend({"received_at": time.time(), "session": None, **row} for row in new)
        return json.dumps(new).encode()
    query = dict(part.split("=", 1) for part in path.split("?", 1)[1].split("&"))
    rows = [row for row in ROWS if row["received_at"] >= float(query.get("received_at", "gte.0")[4:])]
    if "node_id" in query:
        rows = [row for row in rows if str(row["node_id"]) == query["node_id"][3:]]
    if "event_type" in query:
        rows = [row for row in rows if row["event_type"] in query["event_type"][3:].strip("()").split(",")]
    rows.sort(key=lambda row: row[query["order"].split(".")[0]], reverse=query["order"].endswith(".desc"))
    return json.dumps(rows[int(query.get("offset", 0)):][:int(query.get("limit", 1000))]).encode()


_plumbing.supabase = fake_supabase
lumpi.ask_ai = lambda audio: HEARD
os.environ.update(LUMPI_LAN_KEY="key", LUMPI_DASHBOARD_PASSWORD="pw")
SERVER = HTTPServer(("127.0.0.1", 0), lumpi.handler)
threading.Thread(target=SERVER.serve_forever, daemon=True).start()
PASSWORD = {"Authorization": "Basic " + base64.b64encode(b"teacher:pw").decode()}
LAN_KEY = {"Authorization": "Bearer key"}


def call(method, path, body=None, headers=None):
    request = urllib.request.Request(f"http://127.0.0.1:{SERVER.server_address[1]}{path}", data=body,
                                     method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(request) as reply:
            return reply.status, reply.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def event(number, node, kind, at, activity=None, **data):
    return json.dumps({"event_id": f"nest/{number}", "node_id": node, "event_type": kind,
                       "recorded_at": at, "activity": activity, "data": data}).encode()


class CloudTest(unittest.TestCase):
    def setUp(self):
        ROWS.clear()

    def test_telemetry_stores_once_and_rejects_bad(self):
        now = int(time.time())
        room = event(1, 0, "room", now, lux=230.5, distance_cm=None)
        self.assertEqual(call("POST", "/api/telemetry", room, LAN_KEY), (200, '{"result": "added"}'))
        self.assertEqual(call("POST", "/api/telemetry", room, LAN_KEY), (200, '{"result": "duplicate"}'))
        self.assertEqual(call("POST", "/api/telemetry", room, {"Authorization": "Bearer nope"})[0], 401)
        self.assertEqual(call("POST", "/api/telemetry", b"{broken", LAN_KEY)[0], 400)
        self.assertEqual(call("POST", "/api/telemetry", event(2, 7, "room", now, lux=1, distance_cm=1), LAN_KEY)[0], 400)
        self.assertEqual(call("POST", "/api/telemetry", event(3, 1, "status", "now"), LAN_KEY)[0], 400)
        self.assertEqual(call("POST", "/api/telemetry", b"x" * 9000, LAN_KEY)[0], 400)
        self.assertEqual(len(ROWS), 1)

    def test_session_rule(self):
        for number, (at, activity) in enumerate([(5, "book_on"), (10, "idle"), (20, "book_on"), (30, "recording")]):
            ROWS.append({"event_id": f"n/{number}", "node_id": 1, "event_type": "status", "recorded_at": at,
                         "activity": activity, "received_at": at})
        self.assertEqual(lumpi.session_start(1, 40), 20)  # began at the book_on after the idle
        self.assertEqual(lumpi.session_start(2, 40), 40)  # no status seen: the score starts its own session

    def test_grade(self):
        self.assertEqual(lumpi.grade(BOOK["passages"]["1"]["words"], "en-gb", RIGHT)[:3], (1, 0, 100))
        code, word, total, words = lumpi.grade(BOOK["passages"]["1"]["words"], "en-gb", ["ð", "ə", "p", "ɪ", "s", "s", "w", "ɪ", "m", "z"])
        self.assertEqual((code, word), (2, 1))  # "fish" read as "pis": practise word 1
        self.assertEqual(words[1]["phonemes"][0], {"ipa": "f", "score": 0, "heard": "p"})
        self.assertEqual(lumpi.grade(BOOK["passages"]["1"]["words"], "en-gb", [])[0], 3)  # nothing heard
        self.assertEqual(lumpi.norm(["ˈkɑːɹ", "ɚ", "|"], "en-gb"), ["kɑɹ", "ə"])  # stress, length and "|" dropped
        self.assertEqual(lumpi.score_words([("a", ["a"])], ["a", "b"])[0]["extra"], ["b"])  # extra sound kept

    def test_assess_answers_and_stores_score(self):
        global HEARD
        path = "/v1/assess?node=1&attempt=2&passage=1&uid=FFFFFFFF"
        HEARD = ["ð", "ə", "p", "ɪ", "s", "s", "w", "ɪ", "m", "z"]
        self.assertEqual(call("POST", path, b"\0\0" * 100, LAN_KEY),
                         (200, "2,1\n1,fish,f ɪ ʃ,/v1/word_audio?uid=FFFFFFFF&passage=1&word=1"))
        HEARD = RIGHT
        self.assertEqual(call("POST", path, b"\0\0" * 100, LAN_KEY), (200, "1,0"))
        self.assertEqual([row["data"]["score"] for row in ROWS], [78, 100])  # the, fish (1 of 3 sounds), swims
        self.assertEqual(call("POST", path.replace("FFFFFFFF", "AAAAAAAA"), b"\0\0", LAN_KEY), (200, "4,0"))  # no book
        self.assertEqual(call("POST", path.replace("uid=FFFFFFFF", "uid=../x"), b"\0\0", LAN_KEY)[0], 400)
        self.assertEqual(call("POST", path, b"", LAN_KEY)[0], 400)  # no recording

    def test_books_routes(self):
        self.assertEqual(call("GET", "/v1/page?uid=FFFFFFFF&page=1", None, LAN_KEY)[0], 200)
        self.assertEqual(call("GET", "/v1/page?uid=FFFFFFFF&page=2", None, PASSWORD)[0], 404)
        self.assertEqual(call("GET", "/v1/page?uid=FFFFFFFF&page=1")[0], 401)
        self.assertEqual(call("GET", "/v1/word_audio?uid=FFFFFFFF&passage=1&word=1", None, LAN_KEY), (200, "RIFF"))
        self.assertEqual(call("GET", "/v1/word_audio?uid=FFFFFFFF&passage=1&word=0", None, LAN_KEY)[0], 404)  # no clip

    def test_dashboard_views(self):
        now = int(time.time())
        book = {"uid": "FFFFFFFF", "passage": 1, "attempt": 1, "fsr": 3100}
        for number, body in enumerate([event(1, 1, "status", now - 100, "idle", uid=None, passage=None, attempt=None, fsr=0),
                                       event(2, 1, "start", now - 60, "book_on", **book),
                                       event(3, 1, "help", now - 55), event(4, 1, "status", now - 50, "recording", **book)]):
            call("POST", "/api/telemetry", body, LAN_KEY)
        call("POST", "/v1/assess?node=1&attempt=1&passage=1&uid=FFFFFFFF", b"\0\0", LAN_KEY)
        self.assertEqual(call("GET", "/api/dashboard/nodes")[0], 401)
        nodes = {node["node_id"]: node for node in json.loads(call("GET", "/api/dashboard/nodes", None, PASSWORD)[1])}
        self.assertEqual((nodes[1]["status"], nodes[1]["activity"], nodes[1]["help_at"]), ("online", "recording", now - 55))
        self.assertEqual(nodes[0]["status"], "unknown")
        history = lumpi.history(time.time(), 5)
        self.assertEqual(len(history["nodes"]["1"]), 4)  # scores are not history points
        scores = json.loads(call("GET", "/api/dashboard/scores?day=today", None, PASSWORD)[1])["scores"]
        self.assertEqual(scores[0]["session"], now - 60)  # began at book_on, not at the idle status


if __name__ == "__main__":
    unittest.main()
