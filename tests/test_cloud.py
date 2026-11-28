"""Offline test of the cloud API (api/lumpi.py + api/_plumbing.py): Supabase and the AI are
faked in memory, so it needs no network and no account.

    python3 -m unittest tests.test_cloud
"""
import base64
import contextlib
import io
import json
import os
import sys
import threading
import time
import unittest
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from unittest import mock

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

    def test_help_alert_only_for_new_fresh_help(self):
        now = int(time.time())
        help_call = event(10, 1, "help", now)
        with mock.patch.object(lumpi, "send_discord") as send:
            call("POST", "/api/telemetry", help_call, LAN_KEY)
            call("POST", "/api/telemetry", help_call, LAN_KEY)
            call("POST", "/api/telemetry", event(11, 2, "help", now - 400), LAN_KEY)
            call("POST", "/api/telemetry", event(12, 1, "noise", now, db=70), LAN_KEY)
            call("POST", "/api/telemetry", event(13, 1, "status", now, "idle"), LAN_KEY)
            call("POST", "/api/telemetry", event(14, 0, "room", now, lux=100, distance_cm=None), LAN_KEY)
        self.assertEqual(send.call_count, 1)
        self.assertRegex(send.call_args.args[0], r"^Doll 1 needs help \(\d\d:\d\d\) https://lumpi.vercel.app/$")

    def test_help_alert_boundary_and_other_doll(self):
        now = int(time.time())
        with mock.patch.object(lumpi, "send_discord") as send:
            call("POST", "/api/telemetry", event(20, 2, "help", now - 290), LAN_KEY)
            call("POST", "/api/telemetry", event(21, 1, "help", now - 310), LAN_KEY)
        self.assertEqual(send.call_count, 1)
        self.assertTrue(send.call_args.args[0].startswith("Doll 2 needs help"))

    def test_help_alert_time_is_bangkok(self):
        self.assertEqual(lumpi.clock_time(1795000000 - 1795000000 % 86400 + 7 * 3600), "14:00")


    def test_session_rule(self):
        for number, (at, activity) in enumerate([(5, "book_on"), (10, "idle"), (20, "book_on"), (30, "recording")]):
            ROWS.append({"event_id": f"n/{number}", "node_id": 1, "event_type": "status", "recorded_at": at,
                         "activity": activity, "received_at": at})
        self.assertEqual(lumpi.session_start(1, 40), 20)  # began at the book_on after the idle
        self.assertEqual(lumpi.session_start(2, 40), 40)  # no status seen: the score starts its own session

    def test_grade(self):
        self.assertEqual(lumpi.grade(BOOK["passages"]["1"]["words"], RIGHT)[:3], (1, 0, 100))
        code, word, total, first, words = lumpi.grade(BOOK["passages"]["1"]["words"], ["ð", "ə", "p", "ɪ", "s", "s", "w", "ɪ", "m", "z"])
        self.assertEqual((code, word), (2, 1))  # "fish" read as "pis": practise word 1
        self.assertEqual(words[1]["phonemes"][0], {"ipa": "f", "score": 0, "heard": "p"})
        self.assertEqual(lumpi.grade(BOOK["passages"]["1"]["words"], [])[0], 3)  # nothing heard
        self.assertEqual(lumpi.norm(["ˈkɑːɹ", "ɚ", "|"]), ["kɑɹ", "ə"])  # stress, length and "|" dropped
        self.assertEqual(lumpi.score_words([("a", ["a"])], ["a", "b"])[0]["extra"], ["b"])  # extra sound kept

    def test_any_stretch_of_the_page(self):
        """The child may read any part of the page; only the words read are graded (page word numbers)."""
        page = [{"word": w, "ipa": i} for w, i in [
            ("the", "ð ə"), ("cat", "k a t"), ("sat", "s a t"), ("on", "ɒ n"), ("the", "ð ə"), ("mat", "m a t"),
            ("three", "θ ɹ i"), ("green", "ɡ ɹ i n"), ("trees", "t ɹ i z"), ("grow", "ɡ ɹ əʊ"), ("tall", "t ɔ l")]]

        def read(sounds):
            code, worst, total, first, words = lumpi.grade(page, sounds.split())
            return code, worst, first, [word["word"] for word in words]

        self.assertEqual(read("θ ɹ i ɡ ɹ i n t ɹ i z"), (1, 0, 6, ["three", "green", "trees"]))  # middle of the page
        self.assertEqual(read("m a t θ ɹ i ɡ ɹ i n"), (1, 0, 5, ["mat", "three", "green"]))  # across two sentences
        self.assertEqual(read("θ ɹ i ɡ ɹ i n tʃ ɹ i s"), (2, 8, 6, ["three", "green", "trees"]))  # page word 8 wrong
        self.assertEqual(read("f ɹ i ɡ ɹ i n t ɹ i z")[2], 6)  # a wrong first sound still finds the start
        whole = "ð ə k a t s a t ɒ n ð ə m a t θ ɹ i ɡ ɹ i n t ɹ i z ɡ ɹ əʊ t ɔ l"
        self.assertEqual(read(whole)[3], [item["word"] for item in page])
        # Known limit: a repeated word or phrase matches one of its places.
        self.assertEqual(read("θ ɹ i t ɹ i z")[:3], (2, 7, 6))  # "green" skipped: it is the word to practise
        self.assertIn(read("ð ə")[2], [0, 4])  # "the" is on the page twice
        # A word cut off by the 10 s limit, or a first sound the AI missed, is not a word to practise.
        self.assertEqual(read("ð ə k a t s")[:2], (1, 0))
        self.assertEqual(read("ə k a t s a t")[:2], (1, 0))
        self.assertEqual(lumpi.grade([], ["a"])[0], 3)  # an empty page

    def test_reply_lines(self):
        _, _, total, first, words = lumpi.grade(BOOK["passages"]["1"]["words"], ["ð", "ə", "p", "ɪ", "s", "s", "w", "ɪ", "m", "z"])
        lines = lumpi.reply_lines(first, total, words, "/v1/word_audio?uid=FFFFFFFF&passage=1")
        self.assertEqual(lines[:4], ["S,0,3,78", "W,0,r,the,ð ə,ð ə,",
                                     "W,1,w,fish,f ɪ ʃ,p ɪ s,/v1/word_audio?uid=FFFFFFFF&passage=1&word=1",
                                     "W,2,r,swims,s w ɪ m z,s w ɪ m z,"])
        self.assertIn("P,f,w", lines)
        self.assertIn("P,ɪ,r", lines)  # right in both words
        self.assertEqual(len([line for line in lines if line.startswith("P,")]), 9)  # each sound once
        missed = lumpi.score_words([("cat", ["k", "a", "t"])], ["k", "a", "t", "s"])
        self.assertIn(",k a t,k a t +s,", lumpi.reply_lines(0, 75, missed, "/x")[1])  # extra sound as "+s"
        self.assertIn(",k a t,k - t,", lumpi.reply_lines(0, 67, lumpi.score_words([("cat", ["k", "a", "t"])], ["k", "t"]), "/x")[1])

    def test_assess_answers_and_stores_score(self):
        global HEARD
        path = "/v1/assess?node=1&attempt=2&passage=1&uid=FFFFFFFF"
        HEARD = ["ð", "ə", "p", "ɪ", "s", "s", "w", "ɪ", "m", "z"]
        code, reply = call("POST", path, b"\0\0" * 100, LAN_KEY)
        self.assertEqual((code, reply.split("\n")[:3]), (200, ["2,1", "S,0,3,78", "W,0,r,the,ð ə,ð ə,"]))
        HEARD = RIGHT
        self.assertTrue(call("POST", path, b"\0\0" * 100, LAN_KEY)[1].startswith("1,0\nS,0,3,100\n"))
        self.assertEqual([row["data"]["score"] for row in ROWS], [78, 100])  # the, fish (1 of 3 sounds), swims
        self.assertEqual(ROWS[0]["data"]["first_word"], 0)
        HEARD = []
        self.assertEqual(call("POST", path, b"\0\0" * 100, LAN_KEY), (200, "3,0"))  # nothing heard: no more lines
        HEARD = RIGHT
        self.assertEqual(call("POST", path, b"\0\0" * 160000, LAN_KEY)[0], 200)  # 10 s is allowed
        self.assertEqual(call("POST", path, b"\0\0" * 160001, LAN_KEY)[0], 400)  # longer is not
        self.assertEqual(call("POST", path.replace("FFFFFFFF", "AAAAAAAA"), b"\0\0", LAN_KEY), (200, "4,0"))  # no book
        self.assertEqual(call("POST", path.replace("uid=FFFFFFFF", "uid=../x"), b"\0\0", LAN_KEY)[0], 400)
        self.assertEqual(call("POST", path, b"", LAN_KEY)[0], 400)  # no recording

    def test_books_routes(self):
        self.assertEqual(call("GET", "/v1/page?uid=FFFFFFFF&page=1", None, LAN_KEY)[0], 200)
        self.assertEqual(call("GET", "/v1/page?uid=FFFFFFFF&page=2", None, PASSWORD)[0], 404)
        self.assertEqual(call("GET", "/v1/page?uid=FFFFFFFF&page=1")[0], 401)
        self.assertEqual(call("GET", "/v1/book?uid=FFFFFFFF", None, LAN_KEY), (200, "Fish"))
        self.assertEqual(call("GET", "/v1/book?uid=AAAAAAAA", None, LAN_KEY)[0], 404)  # unknown book
        self.assertEqual(call("GET", "/v1/book?uid=FFFFFFFF")[0], 401)
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


class DiscordTest(unittest.TestCase):
    SECRET = "SECRET-TOKEN-123"

    @classmethod
    def setUpClass(cls):
        cls.requests = []
        cls.mode = {"status": 204, "delay": 0}
        requests, mode = cls.requests, cls.mode

        class FakeDiscord(BaseHTTPRequestHandler):
            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                requests.append({"path": self.path, "headers": dict(self.headers), "json": json.loads(body)})
                time.sleep(mode["delay"])
                self.send_response(mode["status"])
                self.send_header("Content-Length", "0")
                self.end_headers()

            def log_message(self, *args):
                pass

        cls.server = HTTPServer(("127.0.0.1", 0), FakeDiscord)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.url = f"http://127.0.0.1:{cls.server.server_address[1]}/api/webhooks/1/{cls.SECRET}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()

    def setUp(self):
        ROWS.clear()
        self.requests.clear()
        self.mode.update(status=204, delay=0)
        os.environ["LUMPI_DISCORD_WEBHOOK"] = self.url

    def tearDown(self):
        os.environ.pop("LUMPI_DISCORD_WEBHOOK", None)

    def send_help(self, number=1, node=1, age_s=0):
        return call("POST", "/api/telemetry", event(number, node, "help", int(time.time()) - age_s), LAN_KEY)

    def test_message_reaches_discord_with_right_text_and_no_mentions(self):
        self.send_help()
        self.assertEqual(len(self.requests), 1)
        sent = self.requests[0]
        self.assertEqual(sent["path"], f"/api/webhooks/1/{self.SECRET}")
        self.assertEqual(sent["headers"]["Content-Type"], "application/json")
        self.assertEqual(sent["json"]["allowed_mentions"], {"parse": []})
        self.assertRegex(sent["json"]["content"], r"^Doll 1 needs help \(\d\d:\d\d\) https://lumpi.vercel.app/$")
        self.assertEqual(set(sent["json"]), {"content", "allowed_mentions"})

    def test_resend_and_old_help_send_nothing(self):
        self.send_help(number=1)
        self.send_help(number=1)
        self.send_help(number=2, age_s=600)
        self.assertEqual(len(self.requests), 1)

    def test_no_webhook_set_still_stores_and_replies(self):
        del os.environ["LUMPI_DISCORD_WEBHOOK"]
        self.assertEqual(self.send_help(), (200, '{"result": "added"}'))
        self.assertEqual(self.requests, [])
        self.assertEqual(len(ROWS), 1)

    def test_discord_error_still_stores_and_replies_and_hides_url(self):
        self.mode["status"] = 500
        log = io.StringIO()
        with contextlib.redirect_stdout(log):
            reply = self.send_help()
        self.assertEqual(reply, (200, '{"result": "added"}'))
        self.assertEqual(len(ROWS), 1)
        self.assertIn("discord alert not sent", log.getvalue())
        self.assertIn("500", log.getvalue())
        self.assertNotIn(self.SECRET, log.getvalue())

    def test_unreachable_discord_hides_url(self):
        os.environ["LUMPI_DISCORD_WEBHOOK"] = f"http://127.0.0.1:1/api/webhooks/1/{self.SECRET}"
        log = io.StringIO()
        with contextlib.redirect_stdout(log):
            reply = self.send_help()
        self.assertEqual(reply, (200, '{"result": "added"}'))
        self.assertIn("discord alert not sent", log.getvalue())
        self.assertNotIn(self.SECRET, log.getvalue())

    def test_slow_discord_gives_up_after_about_3_seconds(self):
        self.mode["delay"] = 6
        log = io.StringIO()
        started = time.monotonic()
        with contextlib.redirect_stdout(log):
            reply = self.send_help()
        took = time.monotonic() - started
        self.assertEqual(reply, (200, '{"result": "added"}'))
        self.assertEqual(len(ROWS), 1)
        self.assertGreater(took, 2.5)
        self.assertLess(took, 4.5)
        self.assertIn("discord alert not sent", log.getvalue())
        self.assertNotIn(self.SECRET, log.getvalue())


if __name__ == "__main__":
    unittest.main()
