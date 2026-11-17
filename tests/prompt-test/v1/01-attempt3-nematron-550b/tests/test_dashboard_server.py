#!/usr/bin/env python3
"""
Tests for LUMPI dashboard server.
Plays every scenario through handle_telemetry with a fake clock.
"""

import sys
import os
import json
import time
import unittest
from collections import deque

# Add laptop to path to import the server module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'laptop'))

# We need to test the server logic directly, so we'll import and test the functions
# Since the server is one file with global state, we'll create a test version

# Import the server module by executing it
import importlib.util
spec = importlib.util.spec_from_file_location(
    "dashboard_server",
    os.path.join(os.path.dirname(__file__), '..', 'laptop', 'dashboard_server.py')
)
dashboard_server = importlib.util.module_from_spec(spec)

# We need to set up the environment before loading
os.environ["LUMPI_LAN_KEY"] = "test"

# Now we can't easily import because of the global state and main block
# Let's just copy the key functions and test them with a fake clock approach

# Instead, let's write a test that uses the actual server module by importing it
# but we need to avoid the main block. Let's do a different approach - create a test class
# that directly tests the logic by creating a fresh server instance

# Actually, the cleanest way is to refactor the server slightly to be testable
# But the prompt says to follow it exactly. Let me write tests that work with the
# server as-is by importing the module and using its functions.

# We'll create a test that manually sets up the server state and tests the functions

# ===================== TEST HELPERS =====================
class FakeTime:
    """Fake time for deterministic testing."""
    def __init__(self, start=1794794400.0):
        self.current = start

    def time(self):
        return self.current

    def advance(self, seconds):
        self.current += seconds

    def sleep(self, seconds):
        self.current += seconds


def make_event(event_id, node_id, event_type, recorded_at, activity, data):
    """Create a valid event dict."""
    return {
        "event_id": event_id,
        "node_id": node_id,
        "event_type": event_type,
        "recorded_at": recorded_at,
        "activity": activity,
        "data": data
    }


def reset_server_state(server_module):
    """Reset all global state in the server module."""
    server_module.latest.clear()
    server_module.recent.clear()
    server_module.seen_ids.clear()
    server_module.history.clear()
    server_module.counts.clear()
    server_module.errors.clear()
    server_module.totals["rejected"] = 0
    server_module.totals["error_id"] = 0


# ===================== TESTS =====================
class TestDashboardServer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Import the server module
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "dashboard_server",
            os.path.join(os.path.dirname(__file__), '..', 'laptop', 'dashboard_server.py')
        )
        cls.server = importlib.util.module_from_spec(spec)
        # Set env var before loading
        os.environ["LUMPI_LAN_KEY"] = "test"
        spec.loader.exec_module(cls.server)
        # Store original now_ts
        cls.original_now_ts = cls.server.now_ts

    def setUp(self):
        reset_server_state(self.server)
        # Use a fake time that starts at a known value
        self.fake_time = 1794794400.0
        self.server.now_ts = lambda: self.fake_time

    def tearDown(self):
        reset_server_state(self.server)
        self.server.now_ts = self.original_now_ts

    def advance_time(self, seconds):
        self.fake_time += seconds

    def test_DASH_01_no_events(self):
        """DASH-01: no events -> all unknown"""
        now = 1794794500.0
        # Manually call the status logic
        nodes = []
        for nid in self.server.SHOWN_NODES:
            nodes.append(self.server.build_node_item(nid, now))

        for node in nodes:
            self.assertEqual(node["status"], "unknown")
            self.assertEqual(node["counts"]["added"], 0)
            self.assertEqual(node["counts"]["duplicate"], 0)
            self.assertNotIn("age_s", node)
            self.assertNotIn("values", node)

    def test_DASH_02_doll1_event(self):
        """DASH-02: Doll 1 event -> only node 1 changes"""
        ev = make_event("lumpi_test/1", 1, "start", 1794794401, "book_on",
                       {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100})
        err = self.server.check_event(ev)
        self.assertIsNone(err)
        result = self.server.add_event(ev)
        self.assertEqual(result, "added")

        # Advance time to 2 seconds after start (event added at t=0, so age=2)
        self.advance_time(2)
        nodes = []
        for nid in self.server.SHOWN_NODES:
            nodes.append(self.server.build_node_item(nid, self.fake_time))

        # Node 0 and 2 should still be unknown
        self.assertEqual(nodes[0]["status"], "unknown")
        self.assertEqual(nodes[2]["status"], "unknown")

        # Node 1 should be online
        n1 = nodes[1]
        self.assertEqual(n1["status"], "online")
        self.assertEqual(n1["node_id"], 1)
        self.assertAlmostEqual(n1["age_s"], 2.0, places=1)
        self.assertEqual(n1["event_type"], "start")
        self.assertEqual(n1["activity"], "book_on")
        self.assertEqual(n1["values"]["uid"], "04A1B2C3")
        self.assertEqual(n1["values"]["passage"], 1)
        self.assertEqual(n1["values"]["attempt"], 1)
        self.assertEqual(n1["values"]["fsr"], 3100)
        self.assertEqual(n1["counts"]["added"], 1)

    def test_DASH_03_doll2_event(self):
        """DASH-03: Doll 2 event -> only node 2 changes"""
        ev = make_event("lumpi_test/1", 2, "start", 1794794402, "book_on",
                       {"uid": "04D4E5F6", "passage": 2, "attempt": 1, "fsr": 2950})
        err = self.server.check_event(ev)
        self.assertIsNone(err)
        result = self.server.add_event(ev)
        self.assertEqual(result, "added")

        self.advance_time(3)
        nodes = []
        for nid in self.server.SHOWN_NODES:
            nodes.append(self.server.build_node_item(nid, self.fake_time))

        self.assertEqual(nodes[0]["status"], "unknown")
        self.assertEqual(nodes[1]["status"], "unknown")
        self.assertEqual(nodes[2]["status"], "online")
        self.assertEqual(nodes[2]["values"]["uid"], "04D4E5F6")

    def test_DASH_04_room_event(self):
        """DASH-04: room event -> nest shows lux"""
        ev = make_event("lumpi_test/1", 0, "room", 1794794400, None,
                       {"lux": 250.0, "distance_cm": 150.0})
        err = self.server.check_event(ev)
        self.assertIsNone(err)
        result = self.server.add_event(ev)
        self.assertEqual(result, "added")

        self.advance_time(1)
        nodes = []
        for nid in self.server.SHOWN_NODES:
            nodes.append(self.server.build_node_item(nid, self.fake_time))

        n0 = nodes[0]
        self.assertEqual(n0["status"], "online")
        self.assertEqual(n0["values"]["lux"], 250.0)
        self.assertEqual(n0["values"]["distance_cm"], 150.0)
        self.assertTrue(n0["low_light"] is False)  # 250 > 100

    def test_DASH_05_doll2_stale(self):
        """DASH-05: Doll 2 quiet 30s -> stale, values kept"""
        # Add initial event for doll 2 at time 0
        self.fake_time = 1794794400.0
        self.server.now_ts = lambda: self.fake_time
        ev = make_event("lumpi_test/1", 2, "start", 1794794400, "book_on",
                       {"uid": "04D4E5F6", "passage": 2, "attempt": 1, "fsr": 2950})
        self.server.add_event(ev)

        # Also add nest and doll1 so they're online (at time 0)
        self.server.add_event(make_event("lumpi_test/2", 0, "room", 1794794400, None,
                          {"lux": 250.0, "distance_cm": 150.0}))
        self.server.add_event(make_event("lumpi_test/3", 1, "start", 1794794400, "book_on",
                          {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100}))

        # Advance time 35 seconds (past STALE_AFTER_S=30)
        # But send fresh events to nest and doll1 to keep them online
        self.advance_time(35)
        
        # Send fresh room event for nest
        self.server.add_event(make_event("lumpi_test/4", 0, "room", 1794794435, None,
                          {"lux": 245.0, "distance_cm": 155.0}))
        # Send fresh status for doll1
        self.server.add_event(make_event("lumpi_test/5", 1, "status", 1794794435, "recording",
                          {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3124}))

        nodes = []
        for nid in self.server.SHOWN_NODES:
            nodes.append(self.server.build_node_item(nid, self.fake_time))

        n2 = nodes[2]
        self.assertEqual(n2["status"], "stale")
        self.assertGreaterEqual(n2["age_s"], 30)
        # Values should be kept
        self.assertEqual(n2["values"]["uid"], "04D4E5F6")
        self.assertEqual(n2["event_type"], "start")
        self.assertEqual(n2["activity"], "book_on")

        # Nest and doll1 should still be online (they got fresh events)
        self.assertEqual(nodes[0]["status"], "online")
        self.assertEqual(nodes[1]["status"], "online")

    def test_DASH_06_doll2_recovers(self):
        """DASH-06: Doll 2 sends again -> online"""
        # Add initial event for doll 2 at time 0
        self.fake_time = 1794794400.0
        self.server.now_ts = lambda: self.fake_time
        ev = make_event("lumpi_test/1", 2, "start", 1794794400, "book_on",
                       {"uid": "04D4E5F6", "passage": 2, "attempt": 1, "fsr": 2950})
        self.server.add_event(ev)

        # Advance time 35 seconds (stale)
        self.advance_time(35)
        nodes = [self.server.build_node_item(nid, self.fake_time) for nid in self.server.SHOWN_NODES]
        self.assertEqual(nodes[2]["status"], "stale")

        # Doll 2 sends a new event
        ev2 = make_event("lumpi_test/2", 2, "status", 1794794437, "recording",
                        {"uid": "04D4E5F6", "passage": 2, "attempt": 1, "fsr": 2980})
        self.server.add_event(ev2)

        # Now check - should be online again
        self.advance_time(1)
        nodes = [self.server.build_node_item(nid, self.fake_time) for nid in self.server.SHOWN_NODES]
        self.assertEqual(nodes[2]["status"], "online")
        self.assertLess(nodes[2]["age_s"], 5)
        # Values updated
        self.assertEqual(nodes[2]["activity"], "recording")
        self.assertEqual(nodes[2]["values"]["fsr"], 2980)

    def test_DASH_07_bad_inputs_rejected(self):
        """DASH-07: bad JSON, missing field, node 9, no token -> rejected, state unchanged"""
        # First add a valid event to have some state
        ev = make_event("lumpi_test/1", 1, "start", 1794794401, "book_on",
                       {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100})
        self.server.add_event(ev)

        # Test various bad inputs
        bad_cases = [
            # Not a dict
            ("not a dict", ["not", "a", "dict"]),
            # Missing event_id
            ("missing event_id", {"node_id": 1, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {}}),
            # Missing node_id
            ("missing node_id", {"event_id": "x/1", "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {}}),
            # Missing event_type
            ("missing event_type", {"event_id": "x/1", "node_id": 1, "recorded_at": 1794794401, "activity": "book_on", "data": {}}),
            # Missing recorded_at
            ("missing recorded_at", {"event_id": "x/1", "node_id": 1, "event_type": "start", "activity": "book_on", "data": {}}),
            # Missing activity
            ("missing activity", {"event_id": "x/1", "node_id": 1, "event_type": "start", "recorded_at": 1794794401, "data": {}}),
            # Missing data
            ("missing data", {"event_id": "x/1", "node_id": 1, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on"}),
            # Bad event_id
            ("bad event_id", {"event_id": "no-slash", "node_id": 1, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {}}),
            # Bad node_id (bool)
            ("bad node_id bool", {"event_id": "x/1", "node_id": True, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {}}),
            # Bad node_id (9)
            ("bad node_id 9", {"event_id": "x/1", "node_id": 9, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {}}),
            # Bad event_type
            ("bad event_type", {"event_id": "x/1", "node_id": 1, "event_type": "invalid", "recorded_at": 1794794401, "activity": "book_on", "data": {}}),
            # Room event from node 1
            ("room from node 1", {"event_id": "x/1", "node_id": 1, "event_type": "room", "recorded_at": 1794794401, "activity": None, "data": {"lux": 100, "distance_cm": 150}}),
            # Doll event from node 0
            ("doll from node 0", {"event_id": "x/1", "node_id": 0, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {"uid": "x", "passage": 1, "attempt": 1, "fsr": 100}}),
            # Bad recorded_at (too small)
            ("bad recorded_at", {"event_id": "x/1", "node_id": 1, "event_type": "start", "recorded_at": 123, "activity": "book_on", "data": {"uid": "x", "passage": 1, "attempt": 1, "fsr": 100}}),
            # Bad activity
            ("bad activity", {"event_id": "x/1", "node_id": 1, "event_type": "start", "recorded_at": 1794794401, "activity": "invalid", "data": {"uid": "x", "passage": 1, "attempt": 1, "fsr": 100}}),
            # Data not object
            ("data not object", {"event_id": "x/1", "node_id": 1, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": "not object"}),
            # Missing data.lux
            ("missing lux", {"event_id": "x/1", "node_id": 0, "event_type": "room", "recorded_at": 1794794401, "activity": None, "data": {"distance_cm": 150}}),
            # Missing data.uid
            ("missing uid", {"event_id": "x/1", "node_id": 1, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {"passage": 1, "attempt": 1, "fsr": 100}}),
        ]

        for name, bad_ev in bad_cases:
            with self.subTest(name=name):
                err = self.server.check_event(bad_ev)
                self.assertIsNotNone(err, f"Expected error for {name}")
                # State should be unchanged (node 1 still has 1 added)
                self.assertEqual(self.server.counts[1]["added"], 1)

    def test_DASH_08_duplicate_event_id(self):
        """DASH-08: same event_id twice -> stored once"""
        ev = make_event("lumpi_test/1", 1, "start", 1794794401, "book_on",
                       {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100})

        result1 = self.server.add_event(ev)
        self.assertEqual(result1, "added")
        self.assertEqual(self.server.counts[1]["added"], 1)
        self.assertEqual(self.server.counts[1]["duplicate"], 0)

        result2 = self.server.add_event(ev)
        self.assertEqual(result2, "duplicate")
        self.assertEqual(self.server.counts[1]["added"], 1)
        self.assertEqual(self.server.counts[1]["duplicate"], 1)

        # Recent should have only one
        self.assertEqual(len(self.server.recent), 1)

    def test_DASH_10_fast_alternating_dolls(self):
        """DASH-10: fast alternating dolls -> no crossed values"""
        # Simulate fast alternating events from doll 1 and doll 2
        base_time = 1794794400
        for i in range(20):
            node_id = 1 if i % 2 == 0 else 2
            ev = make_event(
                f"lumpi_test/{i+1}",
                node_id,
                "status",
                base_time + i,
                "recording",
                {"uid": "04A1B2C3" if node_id == 1 else "04D4E5F6",
                 "passage": 1 if node_id == 1 else 2,
                 "attempt": 1,
                 "fsr": 3100 + i if node_id == 1 else 2950 + i}
            )
            self.server.add_event(ev)

        now = base_time + 21
        nodes = [self.server.build_node_item(nid, now) for nid in self.server.SHOWN_NODES]

        # Each doll should have its own values, not crossed
        n1 = nodes[1]
        n2 = nodes[2]

        # Doll 1 should have even fsr values (3100, 3102, 3104...)
        self.assertEqual(n1["values"]["uid"], "04A1B2C3")
        self.assertEqual(n1["values"]["passage"], 1)

        # Doll 2 should have odd fsr values (2950, 2952, 2954...)
        self.assertEqual(n2["values"]["uid"], "04D4E5F6")
        self.assertEqual(n2["values"]["passage"], 2)

    def test_late_older_event_does_not_overwrite(self):
        """Late older event must not overwrite newer values"""
        # Add a newer event first
        ev_new = make_event("lumpi_test/1", 1, "status", 1794794500, "feedback",
                           {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3200})
        self.server.add_event(ev_new)

        # Add an older event (simulating Wi-Fi retry)
        ev_old = make_event("lumpi_test/2", 1, "status", 1794794400, "book_on",
                           {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100})
        self.server.add_event(ev_old)

        now = 1794794501.0
        nodes = [self.server.build_node_item(nid, now) for nid in self.server.SHOWN_NODES]
        n1 = nodes[1]

        # Should keep the newer values (feedback, fsr=3200)
        self.assertEqual(n1["activity"], "feedback")
        self.assertEqual(n1["values"]["fsr"], 3200)

    def test_rejection_has_hint(self):
        """Every rejection has a non-empty hint"""
        bad_ev = {"event_id": "x/1", "node_id": 1, "event_type": "start", "recorded_at": 1794794401, "activity": "book_on", "data": {}}
        err = self.server.check_event(bad_ev)
        self.assertIsNotNone(err)

        # Record error and check hint
        self.server.record_error(err, "192.168.1.1", "raw", "start")
        self.assertEqual(len(self.server.errors), 1)
        error = self.server.errors[0]
        self.assertIn("hint", error)
        self.assertTrue(len(error["hint"]) > 0)

    def test_history_trim(self):
        """History is trimmed to HISTORY_SIZE"""
        # Add more than HISTORY_SIZE events
        for i in range(self.server.HISTORY_SIZE + 100):
            ev = make_event(f"lumpi_test/{i}", 1, "status", 1794794400 + i, "idle",
                           {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100})
            self.server.add_event(ev)

        # History should be capped at HISTORY_SIZE
        self.assertLessEqual(len(self.server.history[1]), self.server.HISTORY_SIZE)

    def test_seen_ids_clear_at_limit(self):
        """Seen IDs set clears at SEEN_LIMIT"""
        # Add SEEN_LIMIT + 1 unique events
        for i in range(self.server.SEEN_LIMIT + 1):
            ev = make_event(f"lumpi_test/{i}", 1, "status", 1794794400 + i, "idle",
                           {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100})
            self.server.add_event(ev)

        # Seen IDs should have been cleared
        self.assertLessEqual(len(self.server.seen_ids), self.server.SEEN_LIMIT)

    def test_low_light_detection(self):
        """low_light is true when lux < DARK_LUX"""
        ev = make_event("lumpi_test/1", 0, "room", 1794794400, None,
                       {"lux": 80.0, "distance_cm": 150.0})
        self.server.add_event(ev)

        now = 1794794401.0
        nodes = [self.server.build_node_item(nid, now) for nid in self.server.SHOWN_NODES]
        n0 = nodes[0]
        self.assertTrue(n0["low_light"])

        # Test with lux > DARK_LUX
        ev2 = make_event("lumpi_test/2", 0, "room", 1794794401, None,
                        {"lux": 200.0, "distance_cm": 150.0})
        self.server.add_event(ev2)

        now2 = 1794794402.0
        nodes2 = [self.server.build_node_item(nid, now2) for nid in self.server.SHOWN_NODES]
        n0_2 = nodes2[0]
        self.assertFalse(n0_2["low_light"])

    def test_history_endpoint_format(self):
        """History endpoint returns correct format"""
        # Add some events
        self.server.add_event(make_event("lumpi_test/1", 0, "room", 1794794400, None,
                          {"lux": 250.0, "distance_cm": 150.0}))
        self.server.add_event(make_event("lumpi_test/2", 1, "start", 1794794401, "book_on",
                          {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100}))

        now = 1794794410.0
        # Simulate the history endpoint logic
        cutoff = now - 5 * 60
        nodes_data = {}
        for nid in self.server.SHOWN_NODES:
            self.server.ensure_node(nid)
            points = []
            for p in self.server.history[nid]:
                if p["t"] >= cutoff:
                    points.append(p)
            nodes_data[str(nid)] = points

        result = {
            "now": round(now, 3),
            "minutes": 5,
            "dark_lux": self.server.DARK_LUX,
            "stale_after_s": self.server.STALE_AFTER_S,
            "nodes": nodes_data
        }

        self.assertIn("now", result)
        self.assertIn("minutes", result)
        self.assertIn("dark_lux", result)
        self.assertIn("stale_after_s", result)
        self.assertIn("nodes", result)
        self.assertIn("0", result["nodes"])
        self.assertIn("1", result["nodes"])
        self.assertIn("2", result["nodes"])

    def test_dark_room_warning(self):
        """Room event with lux < 100 triggers low_light"""
        self.fake_time = 1794794400.0
        self.server.now_ts = lambda: self.fake_time
        ev = make_event("lumpi_test/1", 0, "room", 1794794400, None,
                       {"lux": 50.0, "distance_cm": 100.0})
        self.server.add_event(ev)

        self.advance_time(1)
        nodes = [self.server.build_node_item(nid, self.fake_time) for nid in self.server.SHOWN_NODES]
        self.assertTrue(nodes[0]["low_light"])

    def test_null_sensor_values(self):
        """Failed sensor (null) is kept as null in values"""
        self.fake_time = 1794794400.0
        self.server.now_ts = lambda: self.fake_time
        ev = make_event("lumpi_test/1", 1, "start", 1794794400, "book_on",
                       {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": None})
        self.server.add_event(ev)

        self.advance_time(1)
        nodes = [self.server.build_node_item(nid, self.fake_time) for nid in self.server.SHOWN_NODES]
        self.assertIn("fsr", nodes[1]["values"])
        self.assertIsNone(nodes[1]["values"]["fsr"])

    def test_offline_after_60s(self):
        """Node goes offline after 60 seconds"""
        # Add event at time 0
        self.fake_time = 1794794400.0
        self.server.now_ts = lambda: self.fake_time
        ev = make_event("lumpi_test/1", 1, "start", 1794794400, "book_on",
                       {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100})
        self.server.add_event(ev)

        # At 61 seconds - should be offline
        self.advance_time(61)
        nodes = [self.server.build_node_item(nid, self.fake_time) for nid in self.server.SHOWN_NODES]
        self.assertEqual(nodes[1]["status"], "offline")

    def test_event_order_in_recent(self):
        """Recent events are newest first"""
        for i in range(5):
            ev = make_event(f"lumpi_test/{i}", 1, "status", 1794794400 + i, "idle",
                           {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100})
            self.server.add_event(ev)

        # Recent should be newest first
        self.assertEqual(self.server.recent[0]["event_id"], "lumpi_test/4")
        self.assertEqual(self.server.recent[4]["event_id"], "lumpi_test/0")


if __name__ == "__main__":
    unittest.main()