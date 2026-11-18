"""Format test: the mock class must pass the server's checks exactly like real hardware.

Run from the project folder:
    python3 -m unittest tests.test_mock_class

Every message from laptop/mock_class.py goes through the same function as
POST /api/telemetry. Then the saved scores are compared with what the mock
sent, and two seeds must give different numbers (nothing hard coded).
"""

import json
import shutil
import tempfile
import time
import unittest
from pathlib import Path

from laptop import dashboard_server as server
from laptop import mock_class


class MockClassTest(unittest.TestCase):
    def setUp(self):
        server.clear_state()
        self.folder = Path(tempfile.mkdtemp())
        self.old = (server.SCORES_DIR, server.ROOM_DIR)
        server.SCORES_DIR = self.folder / "scores"
        server.ROOM_DIR = self.folder / "room"

    def tearDown(self):
        server.SCORES_DIR, server.ROOM_DIR = self.old
        shutil.rmtree(self.folder)

    def play(self, seed, days):
        now = time.time()
        events, expected = mock_class.make_class(seed, days, now)
        for event in events:
            # Same JSON text as the HTTP body the mock sends.
            code, result = server.handle_telemetry(json.dumps(event, ensure_ascii=False), now)
            self.assertEqual((code, result), (200, "added"), event)
        return now, events, expected

    def test_every_message_type_is_sent_and_accepted(self):
        _, events, _ = self.play(seed=1, days=2)
        types = {event["event_type"] for event in events}
        self.assertEqual(types, {"room", "start", "status", "score", "help", "noise"})
        self.assertEqual(server.errors_view()["rejected_total"], 0)

    def test_saved_scores_match_what_was_sent(self):
        now, events, expected = self.play(seed=2, days=3)
        sent = [event["data"] for event in events if event["event_type"] == "score"]
        saved = server.scores_view(now, "past")["scores"]
        self.assertEqual([line["score"] for line in saved], [data["score"] for data in sent])
        self.assertEqual([line["words"] for line in saved], [data["words"] for data in sent])
        self.assertEqual(len(server.scores_view(now, "past")["days"]), 4)

        today = server.scores_view(now, "today")["scores"]
        self.assertEqual(sorted(line["score"] for line in today), sorted(expected["today"]["scores"]))
        # Room readings kept for the past-class analytics, at most one a minute.
        room = server.scores_view(now, "past")["room"]
        self.assertGreater(len(room), 0)
        times = [line["recorded_at"] for line in room]
        self.assertTrue(all(b - a >= server.ROOM_SAVE_EVERY_S for a, b in zip(times, times[1:])))

    def test_every_score_belongs_to_a_reading_session(self):
        now, _, _ = self.play(seed=3, days=0)
        for line in server.scores_view(now, "today")["scores"]:
            self.assertLessEqual(line["session"], line["recorded_at"])
            self.assertGreater(line["recorded_at"] - line["session"], 0)

    def test_doll_state_after_the_class(self):
        self.play(seed=4, days=0)
        nodes = {node["node_id"]: node for node in server.nodes_view(time.time())}
        for node_id in [0, 1, 2]:
            self.assertEqual(nodes[node_id]["status"], "online")
        # Doll 1 pressed help 20 s before the end: the page shows the popup.
        self.assertLess(nodes[1]["help_age_s"], server.STALE_AFTER_S)

    def test_saved_mock_file_is_accepted(self):
        # The fixed file (mock_class.py --load) must pass the same checks, after its times move.
        path = Path(__file__).resolve().parent / "fixtures" / "dashboard" / "mock_class.jsonl"
        # Noon today, so today's class never crosses midnight in this test.
        now = time.mktime(time.localtime()[:3] + (12, 0, 0, 0, 0, -1))
        events = mock_class.load_events(path, now)
        self.assertEqual(max(event["recorded_at"] for event in events), int(now))
        for event in events:
            code, result = server.handle_telemetry(json.dumps(event, ensure_ascii=False), now)
            self.assertEqual((code, result), (200, "added"), event)
        expected = mock_class.expected_from_events(events)
        today = server.scores_view(now, "today")["scores"]
        self.assertEqual(sorted(line["score"] for line in today), sorted(expected["today"]["scores"]))
        self.assertEqual(len(server.scores_view(now, "past")["days"]), 5)

    def test_api_doc_examples_are_accepted(self):
        # Every ```json block in docs/dashboard-api.md must pass the server's checks.
        text = (Path(__file__).resolve().parent.parent / "docs" / "dashboard-api.md").read_text(encoding="utf-8")
        blocks = [part.split("```")[0] for part in text.split("```json")[1:]]
        self.assertEqual(len(blocks), 5)
        for block in blocks:
            code, result = server.handle_telemetry(block, time.time())
            self.assertEqual((code, result), (200, "added"), block)

    def test_other_seed_gives_other_numbers(self):
        first = mock_class.make_class(5, 1, time.time())[1]["today"]["scores"]
        second = mock_class.make_class(6, 1, time.time())[1]["today"]["scores"]
        self.assertNotEqual(first, second)


if __name__ == "__main__":
    unittest.main()
