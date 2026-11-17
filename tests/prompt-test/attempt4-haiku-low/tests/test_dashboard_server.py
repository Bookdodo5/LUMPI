"""Tests for the dashboard server. Scenarios are played with a fake clock, no real waiting."""

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "laptop"))
import dashboard_server as server  # noqa: E402

FIXTURES = ROOT / "tests" / "fixtures" / "dashboard"
KEY = "test"
SOURCE = "192.0.2.10"
START = 1000.0


def send(body, now, token=KEY):
    """Send one body through the same entry point the HTTP handler uses."""
    raw = body.encode("utf-8") if isinstance(body, str) else body
    return server.handle_telemetry(token, str(len(raw)), raw, SOURCE, now)


def read_scenario(name):
    with open(FIXTURES / name, encoding="utf-8") as scenario:
        return [json.loads(line) for line in scenario if line.strip()]


def play(lines, start=START):
    """Play scenario lines with a fake clock. Returns (expected, actual) per line."""
    clock = start
    results = []
    for line in lines:
        clock += line["delay"]
        body = json.dumps(line["event"]) if "event" in line else line["raw"]
        status, payload = send(body, clock)
        results.append((line["expect"], payload.get("result", "rejected")))
    return results


def snapshot():
    # Rejections are counted on purpose, so the "state" here is what the page shows.
    return copy.deepcopy((server.latest, list(server.recent), server.counts))


def node_by_id(now):
    return {item["node_id"]: item for item in server.nodes_payload(now)}


def doll_event(number, node=1, now_recorded=1794794400, fsr=3100, activity="book_on"):
    return {
        "event_id": f"lumpi_1a2b3c4d/{number}", "node_id": node, "event_type": "start",
        "recorded_at": now_recorded, "activity": activity,
        "data": {"uid": "04A1B2C3", "passage": 2, "attempt": 1, "fsr": fsr},
    }


def room_event(number, lux=230.5, distance=160.0):
    return {
        "event_id": f"lumpi_1a2b3c4d/{number}", "node_id": 0, "event_type": "room",
        "recorded_at": 1794794400, "activity": None,
        "data": {"lux": lux, "distance_cm": distance},
    }


class DashboardTest(unittest.TestCase):
    def setUp(self):
        server.LAN_KEY = KEY
        server.reset_state()
        server.record_file = None
        server.last_record_time = None

    def test_scenarios_match_their_expectations(self):
        for path in sorted(FIXTURES.glob("*.jsonl")):
            with self.subTest(scenario=path.name):
                server.reset_state()
                for expected, actual in play(read_scenario(path.name)):
                    self.assertEqual(expected, actual)

    def test_dash01_no_events_all_unknown(self):
        nodes = node_by_id(START)
        self.assertEqual([0, 1, 2], sorted(nodes))
        for item in nodes.values():
            self.assertEqual("unknown", item["status"])
            self.assertEqual({"added": 0, "duplicate": 0}, item["counts"])

    def test_dash02_doll1_event_changes_only_doll1(self):
        send(json.dumps(doll_event(1, node=1)), START)
        nodes = node_by_id(START)
        self.assertEqual("online", nodes[1]["status"])
        self.assertEqual("unknown", nodes[0]["status"])
        self.assertEqual("unknown", nodes[2]["status"])

    def test_dash03_doll2_event_changes_only_doll2(self):
        send(json.dumps(doll_event(1, node=2)), START)
        nodes = node_by_id(START)
        self.assertEqual("online", nodes[2]["status"])
        self.assertEqual("unknown", nodes[1]["status"])

    def test_dash04_room_event_shows_lux(self):
        send(json.dumps(room_event(1, lux=62.7)), START)
        nest = node_by_id(START)[0]
        self.assertEqual(62.7, nest["values"]["lux"])
        self.assertTrue(nest["low_light"])

    def test_dash05_quiet_doll_goes_stale_and_keeps_values(self):
        send(json.dumps(doll_event(1, node=2, fsr=2800)), START)
        nodes = node_by_id(START + 30)
        self.assertEqual("stale", nodes[2]["status"])
        self.assertEqual(2800, nodes[2]["values"]["fsr"])

    def test_dash06_doll_sends_again_becomes_online(self):
        send(json.dumps(doll_event(1, node=2)), START)
        send(json.dumps(doll_event(2, node=2)), START + 40)
        self.assertEqual("online", node_by_id(START + 41)[2]["status"])

    def test_dash07_bad_input_is_rejected_and_changes_nothing(self):
        send(json.dumps(doll_event(1, node=1)), START)
        before = snapshot()
        bad_cases = [
            ("bad json", "{not json", KEY),
            ("missing field", json.dumps({"node_id": 1}), KEY),
            ("node 9", json.dumps(doll_event(2, node=9)), KEY),
            ("no token", json.dumps(doll_event(3, node=1)), None),
        ]
        for name, body, token in bad_cases:
            with self.subTest(case=name):
                status, payload = send(body, START + 1, token=token)
                self.assertIn(status, (400, 401))
                self.assertIn("error", payload)
                self.assertEqual(before, snapshot())

    def test_dash08_same_event_id_stored_once(self):
        body = json.dumps(doll_event(7, node=1))
        self.assertEqual(200, send(body, START)[0])
        self.assertEqual({"result": "duplicate"}, send(body, START + 1)[1])
        counts = node_by_id(START + 1)[1]["counts"]
        self.assertEqual({"added": 1, "duplicate": 1}, counts)
        self.assertEqual(1, len(server.recent))

    def test_dash10_fast_alternating_dolls_no_crossed_values(self):
        lines = read_scenario("busy.jsonl")
        play(lines)
        last = {}
        for line in lines:
            event = line["event"]
            last[event["node_id"]] = event["data"]
        nodes = node_by_id(START + 100)
        self.assertEqual(last[1]["uid"], nodes[1]["values"]["uid"])
        self.assertEqual(last[1]["fsr"], nodes[1]["values"]["fsr"])
        self.assertEqual(last[2]["uid"], nodes[2]["values"]["uid"])
        self.assertEqual(last[2]["fsr"], nodes[2]["values"]["fsr"])

    def test_every_rejection_has_a_hint(self):
        play(read_scenario("bad_packets.jsonl"))
        errors = server.errors_payload()["errors"]
        self.assertEqual(20, len(errors))
        for error in errors:
            self.assertTrue(error["hint"], error["reason"])
            self.assertTrue(error["example"])

    def test_late_older_event_does_not_overwrite_newer_values(self):
        send(json.dumps(doll_event(1, node=1, now_recorded=1794794500, fsr=3300)), START)
        send(json.dumps(doll_event(2, node=1, now_recorded=1794794400, fsr=1111)), START + 1)
        state = node_by_id(START + 1)[1]
        self.assertEqual(3300, state["values"]["fsr"])
        self.assertEqual(1794794500, state["recorded_at"])

    def test_status_comes_from_server_clock(self):
        self.assertEqual("online", server.node_status(29.9))
        self.assertEqual("stale", server.node_status(30))
        self.assertEqual("offline", server.node_status(60))
        self.assertEqual("unknown", server.node_status(None))

    def test_bad_token_stores_no_text(self):
        status, payload = send(json.dumps(doll_event(1, node=1)), START, token="wrong")
        self.assertEqual(401, status)
        error = server.errors_payload()["errors"][0]
        self.assertEqual("bad token", error["reason"])
        self.assertEqual("", error["raw"])

    def test_recorded_file_replays_into_a_fresh_server(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "recorded.jsonl"
            server.record_file = open(path, "w", encoding="utf-8")
            play(read_scenario("flaky_wifi.jsonl")[:12] + read_scenario("bad_packets.jsonl")[:3])
            server.record_file.close()
            server.record_file = None
            first_counts = node_by_id(START + 500)

            server.reset_state()
            recorded = read_scenario_path(path)
            for expected, actual in play(recorded):
                self.assertEqual(expected, actual)
            self.assertEqual(first_counts[2]["counts"], node_by_id(START + 500)[2]["counts"])

    def test_history_keeps_only_the_requested_window(self):
        send(json.dumps(doll_event(1, node=1)), START)
        payload = server.history_payload(5, START + 400)
        self.assertEqual([], payload["nodes"]["1"])
        payload = server.history_payload(15, START + 400)
        self.assertEqual(1, len(payload["nodes"]["1"]))


def read_scenario_path(path):
    with open(path, encoding="utf-8") as scenario:
        return [json.loads(line) for line in scenario if line.strip()]


if __name__ == "__main__":
    unittest.main()
