"""Offline test of api/index.py: a fake Supabase in memory, no network, no account.

    python3 -m unittest tests.test_cloud_api

The fake understands only the query parts api/index.py uses (eq, gte, lte, in,
order, limit, offset, on_conflict).
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
from urllib.parse import parse_qsl

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "api"))
import index as cloud  # noqa: E402

TABLES = {"events": [], "rejects": []}


def matches(row, name, condition):
    op, _, value = condition.partition(".")
    if op == "in":
        return str(row[name]) in value.strip("()").split(",")
    number = float(value) if value.replace(".", "", 1).lstrip("-").isdigit() else value
    if op == "eq":
        return str(row[name]) == str(value)
    if op == "gte":
        return row[name] >= number
    if op == "lte":
        return row[name] <= number
    raise ValueError(op)


def fake_rest(method, path, body=None, headers=None, timeout=20):
    table, _, query = path.partition("?")
    rows = TABLES[table]
    if method == "POST":
        added = []
        for new in body:
            if table == "events" and any(old["event_id"] == new["event_id"] for old in rows):
                continue
            row = {"session": None, "received_at": time.time(), **new}
            rows.append(row)
            added.append(row)
        return added
    result = list(rows)
    limit, offset = None, 0
    for name, value in parse_qsl(query):
        if name == "order":
            column, _, way = value.partition(".")
            result.sort(key=lambda row: row[column], reverse=way == "desc")
        elif name == "limit":
            limit = int(value)
        elif name == "offset":
            offset = int(value)
        elif name != "select":
            result = [row for row in result if matches(row, name, value)]
    result = result[offset:]
    return result[:limit] if limit is not None else result


cloud.rest = fake_rest
TESTS = Path(__file__).resolve().parent
os.environ["LUMPI_BOOKS"] = str(TESTS / "fixtures" / "books.json")  # uid FFFFFFFF, passage 1: "The fish swims."
RIGHT = ["ð", "ə", "f", "ɪ", "ʃ", "s", "w", "ɪ", "m", "z"]  # what the AI hears when all is read right


def event(number, node_id, event_type, at, activity=None, **data):
    return json.dumps({"event_id": f"nest/{number}", "node_id": node_id, "event_type": event_type,
                       "recorded_at": at, "activity": activity, "data": data})


class CloudApiTest(unittest.TestCase):
    def setUp(self):
        TABLES["events"].clear()
        TABLES["rejects"].clear()
        os.environ["LUMPI_DASHBOARD_PASSWORD"] = "pw"

    def test_store_once_and_reject_bad(self):
        now = int(time.time())
        room = event(1, 0, "room", now, lux=230.5, distance_cm=None)
        self.assertEqual(cloud.ingest(room, "test"), (200, "added"))
        self.assertEqual(cloud.ingest(room, "test"), (200, "duplicate"))
        self.assertEqual(len(TABLES["events"]), 1)
        self.assertEqual(cloud.ingest("{broken", "test"), (400, "not JSON"))
        self.assertEqual(cloud.ingest(event(2, 9, "room", now, lux=1.0, distance_cm=1.0), "test")[0], 400)
        self.assertEqual(len(TABLES["rejects"]), 2)
        self.assertEqual([error["reason"] == "not JSON" for error in cloud.errors_view()["errors"]], [False, True])  # newest first

    def test_score_session_and_views(self):
        now = int(time.time())
        book = {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "fsr": 3100}
        cloud.ingest(event(1, 1, "status", now - 100, "idle", uid=None, passage=None, attempt=None, fsr=10), "t")
        cloud.ingest(event(2, 1, "start", now - 60, "book_on", **book), "t")
        cloud.ingest(event(3, 1, "status", now - 50, "recording", **book), "t")
        words = [{"word": "fish", "score": 90, "phonemes": [{"ipa": "f", "score": 95}]}]
        score = json.dumps({"event_id": "laptop_ai/1", "node_id": 1, "event_type": "score", "recorded_at": now - 40,
                            "activity": None, "data": {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "score": 90, "words": words}})
        self.assertEqual(cloud.ingest(score, "ai"), (200, "added"))

        today = cloud.scores_view(time.time(), "today")
        self.assertEqual(today["scores"][0]["session"], now - 60)  # began at book_on, not at the idle status

        nodes = {node["node_id"]: node for node in cloud.nodes_view(time.time())}
        self.assertEqual(nodes[1]["status"], "online")
        self.assertEqual(nodes[1]["activity"], "recording")
        self.assertEqual(len(cloud.history_view(time.time(), 5)["nodes"]["1"]), 3)  # scores are not history points

    def test_session_rule(self):
        rows = [{"recorded_at": 30, "activity": "recording"}, {"recorded_at": 20, "activity": "book_on"},
                {"recorded_at": 10, "activity": "idle"}, {"recorded_at": 5, "activity": "book_on"}]
        self.assertEqual(cloud.session_start(rows, 40), 20)
        self.assertEqual(cloud.session_start([], 40), 40)  # no status seen: the score starts its own session

    def test_password(self):
        good = "Basic " + __import__("base64").b64encode(b"teacher:pw").decode()
        self.assertTrue(cloud.password_ok(good))
        self.assertFalse(cloud.password_ok(""))
        os.environ["LUMPI_DASHBOARD_PASSWORD"] = ""
        self.assertFalse(cloud.password_ok(good))  # no password set = locked, never open

    def test_align(self):
        self.assertEqual(cloud.align(list("abc"), list("abc")), [("a", "a"), ("b", "b"), ("c", "c")])
        self.assertEqual(cloud.align(list("abc"), list("axc")), [("a", "a"), ("b", "x"), ("c", "c")])  # said x instead of b
        self.assertEqual(cloud.align(list("abc"), list("ac")), [("a", "a"), ("b", None), ("c", "c")])  # missed b
        self.assertEqual(cloud.align(list("ac"), list("abc")), [("a", "a"), (None, "b"), ("c", "c")])  # extra b
        self.assertEqual(cloud.norm(["ˈθ", "ɹ", "iː", "|", "ɚ", "ɑ5"], "en-gb"), ["θ", "ɹ", "i", "ə", "ɑ"])

    def test_grade(self):
        language, words = cloud.passage_of("FFFFFFFF", 1)
        self.assertEqual(cloud.grade(words, language, RIGHT)[:3], (1, 0, 100))
        code, word, _, result = cloud.grade(words, language, RIGHT[:5] + ["s", "ɪ", "m", "z"])  # "w" missed in swims
        self.assertEqual((code, word), (1, 0))  # swims 4/5 = 80: still passes
        self.assertEqual(result[2]["phonemes"][1], {"ipa": "w", "score": 0, "heard": None})
        code, word, _, result = cloud.grade(words, language, ["ð", "ə", "p", "ɪ", "s"] + RIGHT[5:])  # fish read as "pis"
        self.assertEqual((code, word, result[1]["score"]), (2, 1, 33))
        self.assertEqual(result[1]["pairs"], [["f", "p"], ["ɪ", "ɪ"], ["ʃ", "s"]])
        _, _, _, result = cloud.grade(words, language, RIGHT + ["ʔ"])  # an extra sound joins the word before it
        self.assertEqual((result[2]["score"], result[2]["pairs"][-1]), (83, [None, "ʔ"]))  # 5 right of 6 pairs
        self.assertEqual(cloud.grade(words, language, [])[:2], (3, 0))  # nothing heard
        self.assertIsNone(cloud.ds.check_words(result))  # the stored words pass the dashboard's check

    def test_assess(self):
        heard = {"phones": RIGHT}
        real_ask_ai, cloud.ask_ai = cloud.ask_ai, lambda *args: heard
        self.addCleanup(setattr, cloud, "ask_ai", real_ask_ai)
        self.assertEqual(cloud.assess(1, 1, 1, "FFFFFFFF", b"\0\0"), "1,0")
        heard["phones"] = RIGHT[:5] + ["ʔ"] * 5
        self.assertEqual(cloud.assess(1, 2, 1, "FFFFFFFF", b"\0\0"),
                         "2,2\n2,swims,s w ɪ m z,/v1/word_audio?uid=FFFFFFFF&passage=1&word=2")  # + one line per wrong word
        heard["phones"] = ["ʔ"] * 5 + RIGHT[5:]  # "the" and "fish" wrong: two extra lines, in passage order
        self.assertEqual(cloud.assess(1, 2, 1, "FFFFFFFF", b"\0\0").split("\n"),
                         ["2,0", "0,the,ð ə,/v1/word_audio?uid=FFFFFFFF&passage=1&word=0",
                          "1,fish,f ɪ ʃ,/v1/word_audio?uid=FFFFFFFF&passage=1&word=1"])
        self.assertEqual(len([row for row in TABLES["events"] if row["event_type"] == "score"]), 3)
        self.assertEqual(cloud.assess(1, 3, 2, "FFFFFFFF", b"\0\0"), "4,0")  # unknown passage
        self.assertEqual(cloud.assess(1, 3, 1, "ABCDEF01", b"\0\0"), "4,0")  # unknown book
        heard["phones"] = "not a list"
        self.assertEqual(cloud.assess(1, 4, 1, "FFFFFFFF", b"\0\0"), "4,0")  # AI answered nonsense
        self.assertEqual(len(TABLES["rejects"]), 3)
        self.assertEqual(cloud.check_assess({"node": ["1"], "attempt": ["1"], "passage": ["999"], "uid": ["FFFFFFFF"]}, 2)[2], 999)
        for page in ["0", "1000", "07", ""]:
            with self.assertRaises(ValueError):
                cloud.check_assess({"node": ["1"], "attempt": ["1"], "passage": [page], "uid": ["FFFFFFFF"]}, 2)

    def test_british_sounds(self):
        # British English is the base: "car" = k ɑː (no r). The model's fused and r-coloured tokens
        # become British vowels, and a book's centring diphthong ("here" = h iə) matches the fused "ɪɹ".
        for book, heard in [(["k", "ɑː"], ["k", "ɑːɹ"]), (["w", "ɔː", "t", "ə"], ["w", "ɔː", "t", "ɚ"]),
                            (["h", "iə"], ["h", "ɪɹ"]), (["f", "aɪə"], ["f", "aɪɚ"]), (["t", "eɪ", "b", "əl"], ["t", "eɪ", "b", "l̩"])]:
            self.assertEqual(cloud.norm(book, "en-gb"), cloud.norm(heard, "en-gb"), book)
        car = [{"word": "car", "ipa": "k ɑː"}]
        self.assertEqual(cloud.grade(car, "en-gb", ["k", "ɑːɹ"])[:3], (1, 0, 100))
        self.assertEqual(cloud.grade(car, "en-gb", ["k", "ɑː", "ɹ"])[:3], (1, 0, 67))  # a separate American "r": one extra sound, still a pass

    def test_longest_page_passes_the_dashboard_check(self):
        words = [{"word": "fish", "ipa": "f ɪ ʃ"}] * cloud.book_store.MAX_WORDS
        self.assertEqual(cloud.book_store.MAX_WORDS, cloud.ds.MAX_WORDS)
        _, _, total, result = cloud.grade(words, "en-gb", ["f", "ɪ", "ʃ"] * len(words))
        score = {"event_id": "cloud_ai/1-1-1", "node_id": 1, "event_type": "score", "recorded_at": int(time.time()),
                 "activity": None, "data": {"uid": "FFFFFFFF", "passage": 1, "attempt": 1, "score": total, "words": result}}
        self.assertIsNone(cloud.ds.check_event(score))

    def test_answer_survives_database_down(self):
        real = cloud.ask_ai, cloud.rest
        self.addCleanup(lambda: (setattr(cloud, "ask_ai", real[0]), setattr(cloud, "rest", real[1])))
        given = {}
        cloud.ask_ai = lambda audio, uid, passage, attempt, timeout: given.setdefault("timeout", timeout) and {"phones": RIGHT}

        def down(*args, **kwargs):
            raise urllib.error.URLError("database down")
        cloud.rest = down
        self.assertEqual(cloud.assess(1, 1, 1, "FFFFFFFF", b"\0\0", time.monotonic() - 4), "1,0")  # graded, not "4,0"
        self.assertTrue(6 < given["timeout"] <= 7)  # the AI gets what is left of the 11 s (4 s already used)

    def test_word_audio_and_page(self):
        os.environ["LUMPI_LAN_KEY"] = "lan-key-for-tests"
        real_storage = cloud.book_store.storage
        # Supabase Storage faked: only fish has a clip. The book itself comes from LUMPI_BOOKS.
        cloud.book_store.storage = lambda method, path, *rest: b"RIFF-fake" if path == "object/books/words/en-gb/fish.wav" else None
        server = HTTPServer(("127.0.0.1", 0), cloud.handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()

        def get(path, query, key="lan-key-for-tests"):
            request = urllib.request.Request(f"http://127.0.0.1:{server.server_port}/v1/{path}?{query}",
                                             headers={"Authorization": "Bearer " + key})
            try:
                with urllib.request.urlopen(request) as reply:
                    return reply.status, reply.headers["Content-Type"], reply.read()
            except urllib.error.HTTPError as error:
                return error.code, error.headers["Content-Type"], error.read()
        try:
            self.assertEqual(get("word_audio", "uid=FFFFFFFF&passage=1&word=1"), (200, "audio/wav", b"RIFF-fake"))
            self.assertEqual(get("word_audio", "uid=FFFFFFFF&passage=1&word=0")[::2], (404, b"clip not recorded yet"))
            for bad in ["uid=FFFFFFFF&passage=1&word=3", "uid=FFFFFFFF&passage=1&word=-1",
                        "uid=FFFFFFFF&passage=9&word=0", "uid=_help&passage=1&word=0", "uid=FFFFFFFF&passage=01&word=0", "uid=FFFFFFFF&passage=1&word=../x"]:
                self.assertEqual(get("word_audio", bad)[::2], (404, b"no such book/passage/word"), bad)
            self.assertEqual(get("word_audio", "uid=FFFFFFFF&passage=1&word=1", key="wrong")[0], 401)
            self.assertEqual(TABLES["rejects"], [])  # an unauthenticated GET writes nothing to the database
            self.assertEqual(get("word_audio", "uid=FFFFFFFF&passage=1&word=1", key="café")[0], 401)  # non-ASCII: 401, not 500
            request = urllib.request.Request(f"http://127.0.0.1:{server.server_port}/v1/word_audio?uid=FFFFFFFF&passage=1&word=1",
                                             headers={"Authorization": "Basic " + base64.b64encode(b"teacher:pw").decode()})
            with urllib.request.urlopen(request) as reply:
                self.assertEqual(reply.status, 200)  # the teacher's dashboard password works too

            code, _, body = get("page", "uid=FFFFFFFF&page=1")
            page = json.loads(body)
            self.assertEqual((code, page["text"], page["words"][1]),
                             (200, "The fish swims.", {"word": "fish", "ipa": "f ɪ ʃ", "audio": "/v1/word_audio?uid=FFFFFFFF&passage=1&word=1"}))
            for bad in ["uid=FFFFFFFF&page=2", "uid=FFFFFFFF&page=01", "uid=_help&page=1", "uid=&page="]:
                self.assertEqual(get("page", bad)[0], 404, bad)
            self.assertEqual(get("page", "uid=FFFFFFFF&page=1", key="wrong")[0], 401)

            real_load_book = cloud.load_book
            cloud.load_book = lambda uid: {"title": "broken", "language": "en-gb", "passages": {"1": {"text": "no words list"}}}  # wrong shape
            try:
                self.assertEqual(get("word_audio", "uid=FFFFFFFF&passage=1&word=1")[0], 502)
                self.assertEqual(get("page", "uid=FFFFFFFF&page=1")[0], 502)
            finally:
                cloud.load_book = real_load_book
        finally:
            server.shutdown()
            server.server_close()
            cloud.book_store.storage = real_storage


if __name__ == "__main__":
    unittest.main()
