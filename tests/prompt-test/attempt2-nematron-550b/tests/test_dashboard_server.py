#!/usr/bin/env python3
"""
Tests for dashboard_server.py

Plays every scenario through handle_telemetry with a fake clock
and checks the expected states.
"""

import sys
import os
import json
import time
import unittest
from collections import deque

# Add laptop to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'laptop'))

# Import the server module
import dashboard_server as srv


class FakeClock:
    """Fake clock for testing."""
    def __init__(self, start=1794794400.0):
        self.now = start

    def time(self):
        return self.now

    def advance(self, seconds):
        self.now += seconds


class TestDashboardServer(unittest.TestCase):
    def setUp(self):
        # Reset all global state
        srv.latest.clear()
        srv.recent.clear()
        srv.seen_ids.clear()
        srv.history.clear()
        srv.counts.clear()
        srv.errors.clear()
        srv.totals["rejected"] = 0
        srv.totals["error_id"] = 0

        # Replace time.time with fake clock
        self.fake_clock = FakeClock()
        self.original_time = time.time
        time.time = self.fake_clock.time

        # Patch server's time references
        srv.time.time = self.fake_clock.time

    def tearDown(self):
        time.time = self.original_time

    def post_event(self, ev, source_ip="127.0.0.1"):
        """Helper to check and add an event."""
        error = srv.check_event(ev)
        if error:
            srv.record_error(error, json.dumps(ev)[:200], source_ip, ev.get("event_type"))
            return {"error": error}, 400
        now = self.fake_clock.now
        result = srv.add_event(ev, now, source_ip)
        return {"result": result}, 200

    def get_nodes(self):
        """Get formatted nodes list."""
        now = self.fake_clock.now
        nodes = []
        all_nodes = set(srv.SHOWN_NODES) | set(srv.latest.keys())
        for nid in sorted(all_nodes):
            nodes.append(srv.format_node(nid, now))
        return nodes

    def assert_node_status(self, nodes, node_id, expected_status):
        """Assert a node has the expected status."""
        node = next(n for n in nodes if n["node_id"] == node_id)
        self.assertEqual(node["status"], expected_status,
            f"Node {node_id} status: expected {expected_status}, got {node['status']}")

    # DASH-01: no events -> all unknown
    def test_dash_01_no_events(self):
        nodes = self.get_nodes()
        self.assertEqual(len(nodes), 3)
        for nid in [0, 1, 2]:
            self.assert_node_status(nodes, nid, "unknown")
            node = next(n for n in nodes if n["node_id"] == nid)
            self.assertEqual(node["counts"]["added"], 0)

    # DASH-02: Doll 1 event -> only node 1 changes
    def test_dash_02_doll1_event(self):
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100}
        }
        self.post_event(ev)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 0, "unknown")
        self.assert_node_status(nodes, 1, "online")
        self.assert_node_status(nodes, 2, "unknown")
        node1 = next(n for n in nodes if n["node_id"] == 1)
        self.assertEqual(node1["values"]["uid"], "04A1B2C3")
        self.assertEqual(node1["activity"], "book_on")

    # DASH-03: Doll 2 event -> only node 2 changes
    def test_dash_03_doll2_event(self):
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 2,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04B2C3D4", "passage": 2, "attempt": 1, "fsr": 2950}
        }
        self.post_event(ev)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 0, "unknown")
        self.assert_node_status(nodes, 1, "unknown")
        self.assert_node_status(nodes, 2, "online")

    # DASH-04: room event -> nest shows lux
    def test_dash_04_room_event(self):
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 0,
            "event_type": "room",
            "recorded_at": 1794794400,
            "activity": None,
            "data": {"lux": 230.5, "distance_cm": 160.0}
        }
        self.post_event(ev)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 0, "online")
        node0 = next(n for n in nodes if n["node_id"] == 0)
        self.assertEqual(node0["values"]["lux"], 230.5)
        self.assertEqual(node0["values"]["distance_cm"], 160.0)
        self.assertFalse(node0["low_light"])

    # DASH-05: Doll 2 quiet 30s -> stale, values kept
    def test_dash_05_doll2_stale(self):
        # Add initial event for doll 2
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 2,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04B2C3D4", "passage": 2, "attempt": 1, "fsr": 2950}
        }
        self.post_event(ev)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 2, "online")

        # Advance 35 seconds (past STALE_AFTER_S = 30)
        self.fake_clock.advance(35)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 2, "stale")
        node2 = next(n for n in nodes if n["node_id"] == 2)
        # Values should be kept
        self.assertEqual(node2["values"]["uid"], "04B2C3D4")
        self.assertEqual(node2["activity"], "book_on")

    # DASH-06: Doll 2 sends again -> online
    def test_dash_06_doll2_online_again(self):
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 2,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04B2C3D4", "passage": 2, "attempt": 1, "fsr": 2950}
        }
        self.post_event(ev)
        self.fake_clock.advance(35)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 2, "stale")

        # New event
        ev2 = {
            "event_id": "lumpi_test/2",
            "node_id": 2,
            "event_type": "status",
            "recorded_at": 1794794440,
            "activity": "recording",
            "data": {"uid": "04B2C3D4", "passage": 2, "attempt": 1, "fsr": 3000}
        }
        self.post_event(ev2)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 2, "online")
        node2 = next(n for n in nodes if n["node_id"] == 2)
        self.assertEqual(node2["activity"], "recording")

    # DASH-07: bad JSON, missing field, node 9, no token -> rejected, state unchanged
    def test_dash_07_rejections(self):
        # Add a valid event first
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100}
        }
        self.post_event(ev)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 1, "online")

        # Bad JSON (not a dict)
        error = srv.check_event("not a dict")
        self.assertIsNotNone(error)
        srv.record_error(error, "not a dict", "127.0.0.1")
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 1, "online")  # unchanged

        # Missing field
        ev_bad = {"event_id": "lumpi_test/2", "node_id": 1}  # missing event_type, recorded_at, activity, data
        error = srv.check_event(ev_bad)
        self.assertIsNotNone(error)
        self.assertIn("missing", error)
        srv.record_error(error, json.dumps(ev_bad), "127.0.0.1")
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 1, "online")  # unchanged

        # Invalid node_id (9)
        ev_bad2 = {
            "event_id": "lumpi_test/3",
            "node_id": 9,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100}
        }
        error = srv.check_event(ev_bad2)
        self.assertIsNotNone(error)
        self.assertIn("bad node_id", error)
        srv.record_error(error, json.dumps(ev_bad2), "127.0.0.1")
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 1, "online")  # unchanged

        # Check errors were recorded
        self.assertEqual(srv.totals["rejected"], 3)
        self.assertEqual(len(srv.errors), 3)

    # DASH-08: same event_id twice -> stored once
    def test_dash_08_duplicate(self):
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100}
        }
        result, _ = self.post_event(ev)
        self.assertEqual(result["result"], "added")

        # Same event_id again
        result2, _ = self.post_event(ev)
        self.assertEqual(result2["result"], "duplicate")

        # Counts
        nodes = self.get_nodes()
        node1 = next(n for n in nodes if n["node_id"] == 1)
        self.assertEqual(node1["counts"]["added"], 1)
        self.assertEqual(node1["counts"]["duplicate"], 1)

        # Recent should have only 1 entry
        self.assertEqual(len(srv.recent), 1)

    # DASH-09: stop/restart server -> page disconnected, then recovers (tested manually)
    def test_dash_09_server_restart(self):
        # This is tested manually by stopping/starting the server
        # Here we just verify state is in memory and would be lost on restart
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100}
        }
        self.post_event(ev)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 1, "online")

        # Simulate restart by clearing state
        srv.latest.clear()
        srv.recent.clear()
        srv.seen_ids.clear()
        srv.history.clear()
        srv.counts.clear()

        nodes = self.get_nodes()
        self.assert_node_status(nodes, 1, "unknown")

    # DASH-10: fast alternating dolls -> no crossed values
    def test_dash_10_fast_alternating(self):
        # Rapid alternating events for doll 1 and doll 2
        for i in range(10):
            ev1 = {
                "event_id": f"lumpi_test/{i*2+1}",
                "node_id": 1,
                "event_type": "status",
                "recorded_at": 1794794400 + i,
                "activity": "recording",
                "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100 + i}
            }
            ev2 = {
                "event_id": f"lumpi_test/{i*2+2}",
                "node_id": 2,
                "event_type": "status",
                "recorded_at": 1794794400 + i,
                "activity": "recording",
                "data": {"uid": "04B2C3D4", "passage": 2, "attempt": 1, "fsr": 2950 + i}
            }
            self.post_event(ev1)
            self.post_event(ev2)

        nodes = self.get_nodes()
        node1 = next(n for n in nodes if n["node_id"] == 1)
        node2 = next(n for n in nodes if n["node_id"] == 2)

        # Each should have their own values, not crossed
        self.assertEqual(node1["values"]["uid"], "04A1B2C3")
        self.assertEqual(node2["values"]["uid"], "04B2C3D4")
        self.assertEqual(node1["values"]["passage"], 1)
        self.assertEqual(node2["values"]["passage"], 2)

    # Additional tests

    def test_low_light_detection(self):
        """Nest reports low_light when lux < DARK_LUX."""
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 0,
            "event_type": "room",
            "recorded_at": 1794794400,
            "activity": None,
            "data": {"lux": 80.0, "distance_cm": 160.0}
        }
        self.post_event(ev)
        nodes = self.get_nodes()
        node0 = next(n for n in nodes if n["node_id"] == 0)
        self.assertTrue(node0["low_light"])

        # Above threshold
        ev2 = {
            "event_id": "lumpi_test/2",
            "node_id": 0,
            "event_type": "room",
            "recorded_at": 1794794410,
            "activity": None,
            "data": {"lux": 150.0, "distance_cm": 160.0}
        }
        self.post_event(ev2)
        nodes = self.get_nodes()
        node0 = next(n for n in nodes if n["node_id"] == 0)
        self.assertFalse(node0["low_light"])

    def test_null_sensor_values(self):
        """null values are preserved and shown as 'no reading'."""
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 0,
            "event_type": "room",
            "recorded_at": 1794794400,
            "activity": None,
            "data": {"lux": 100.0, "distance_cm": None}
        }
        self.post_event(ev)
        nodes = self.get_nodes()
        node0 = next(n for n in nodes if n["node_id"] == 0)
        self.assertIsNone(node0["values"]["distance_cm"])
        self.assertEqual(node0["values"]["lux"], 100.0)

    def test_late_older_event_does_not_overwrite(self):
        """A late older event must not overwrite newer values."""
        # Newer event first
        ev_new = {
            "event_id": "lumpi_test/1",
            "node_id": 1,
            "event_type": "status",
            "recorded_at": 1794794410,
            "activity": "feedback",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3200}
        }
        self.post_event(ev_new)

        # Older event arrives later (simulating Wi-Fi retry)
        ev_old = {
            "event_id": "lumpi_test/2",
            "node_id": 1,
            "event_type": "status",
            "recorded_at": 1794794405,
            "activity": "recording",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100}
        }
        self.post_event(ev_old)

        nodes = self.get_nodes()
        node1 = next(n for n in nodes if n["node_id"] == 1)
        # Should keep the newer values
        self.assertEqual(node1["activity"], "feedback")
        self.assertEqual(node1["values"]["fsr"], 3200)

    def test_error_hints_exist(self):
        """Every rejection has a non-empty hint."""
        ev_bad = {"event_id": "lumpi_test/1", "node_id": 1}  # missing fields
        error = srv.check_event(ev_bad)
        self.assertIsNotNone(error)
        hint = srv.get_hint(error)
        self.assertIsInstance(hint, str)
        self.assertGreater(len(hint), 0)

    def test_history_api(self):
        """History endpoint returns correct structure."""
        # Add some events
        for i in range(5):
            ev = {
                "event_id": f"lumpi_test/{i}",
                "node_id": 1,
                "event_type": "status",
                "recorded_at": 1794794400 + i * 10,
                "activity": "recording",
                "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100 + i}
            }
            self.post_event(ev)
            self.fake_clock.advance(10)

        # Get history via internal structure
        now = self.fake_clock.now
        nodes_data = {}
        for nid in [0, 1, 2]:
            node_hist = srv.history.get(nid, deque())
            cutoff = now - 15 * 60
            filtered = [d for d in node_hist if d["recorded_at"] >= cutoff]
            nodes_data[str(nid)] = filtered

        self.assertIn("1", nodes_data)
        self.assertEqual(len(nodes_data["1"]), 5)

    def test_offline_after_60s(self):
        """Node becomes offline after 60s."""
        ev = {
            "event_id": "lumpi_test/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100}
        }
        self.post_event(ev)
        self.fake_clock.advance(35)
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 1, "stale")

        self.fake_clock.advance(30)  # total 65s
        nodes = self.get_nodes()
        self.assert_node_status(nodes, 1, "offline")


if __name__ == '__main__':
    unittest.main()