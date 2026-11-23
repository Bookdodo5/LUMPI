"""Offline test of api/index.py: a fake Supabase in memory, no network, no account.

    python3 -m unittest tests.test_cloud_api

The fake understands only the query parts api/index.py uses (eq, gte, lte, in,
order, limit, offset, on_conflict).
"""
import json
import os
import sys
import time
import unittest
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


def fake_rest(method, path, body=None, headers=None):
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


if __name__ == "__main__":
    unittest.main()
