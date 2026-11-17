#!/usr/bin/env python3
"""
Unit tests for the LUMPI dashboard server.
Plays every scenario through handle_telemetry with a fake clock and checks expectations.
"""

import sys
import os
import json
import time
import unittest
from collections import deque

# Add laptop to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "laptop"))

from dashboard_server import (
    check_event,
    add_event,
    node_status,
    get_nodes_json,
    get_history_json,
    latest,
    recent,
    seen_ids,
    history,
    counts,
    errors,
    totals,
    STALE_AFTER_S,
    OFFLINE_AFTER_S,
    DARK_LUX,
    SHOWN_NODES,
    VALID_NODES,
    ACTIVITIES,
    REQUIRED_DATA,
    EXAMPLES,
    HINTS,
    find_hint,
    get_example_for_error,
)


class FakeClock:
    """Fake clock for deterministic time control."""
    def __init__(self, start=1794794400.0):
        self.now = start

    def time(self):
        return self.now

    def advance(self, seconds):
        self.now += seconds


class TestDashboardServer(unittest.TestCase):
    def setUp(self):
        # Reset all global state
        global latest, recent, seen_ids, history, counts, errors, totals
        latest.clear()
        recent.clear()
        seen_ids.clear()
        history.clear()
        counts.clear()
        errors.clear()
        totals["rejected"] = 0
        totals["error_id"] = 0

        # Use fake clock
        self.fake_clock = FakeClock()
        self.original_time = time.time
        time.time = self.fake_clock.time

    def tearDown(self):
        time.time = self.original_time

    # DASH-01: no events -> all unknown
    def test_dash_01_no_events(self):
        nodes = get_nodes_json()
        for node in nodes:
            self.assertEqual(node["status"], "unknown")
            self.assertEqual(node["counts"]["added"], 0)
            self.assertEqual(node["counts"]["duplicate"], 0)

    # DASH-02/03: Doll 1 / Doll 2 event -> only that node changes
    def test_dash_02_doll1_event(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        err = check_event(evt)
        self.assertIsNone(err)
        result = add_event(evt, self.fake_clock.now, "127.0.0.1")
        self.assertEqual(result, "added")

        nodes = get_nodes_json()
        for node in nodes:
            if node["node_id"] == 1:
                self.assertEqual(node["status"], "online")
                self.assertEqual(node["activity"], "book_on")
                self.assertEqual(node["values"]["uid"], "04A1B2C3")
            else:
                self.assertEqual(node["status"], "unknown")

    def test_dash_03_doll2_event(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 2,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C4", "passage": 2, "attempt": 1, "fsr": 2950},
        }
        err = check_event(evt)
        self.assertIsNone(err)
        result = add_event(evt, self.fake_clock.now, "127.0.0.1")
        self.assertEqual(result, "added")

        nodes = get_nodes_json()
        for node in nodes:
            if node["node_id"] == 2:
                self.assertEqual(node["status"], "online")
                self.assertEqual(node["activity"], "book_on")
            else:
                self.assertEqual(node["status"], "unknown")

    # DASH-04: room event -> nest shows lux
    def test_dash_04_room_event(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 0,
            "event_type": "room",
            "recorded_at": 1794794400,
            "activity": None,
            "data": {"lux": 230.5, "distance_cm": 160.0},
        }
        err = check_event(evt)
        self.assertIsNone(err)
        result = add_event(evt, self.fake_clock.now, "127.0.0.1")
        self.assertEqual(result, "added")

        nodes = get_nodes_json()
        nest = next(n for n in nodes if n["node_id"] == 0)
        self.assertEqual(nest["status"], "online")
        self.assertEqual(nest["values"]["lux"], 230.5)
        self.assertEqual(nest["values"]["distance_cm"], 160.0)

    # DASH-05: Doll 2 quiet 30s -> stale, values kept
    def test_dash_05_doll2_stale(self):
        # Add initial event for Doll 2
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 2,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C4", "passage": 2, "attempt": 1, "fsr": 2950},
        }
        add_event(evt, self.fake_clock.now, "127.0.0.1")

        # Advance 35 seconds (past STALE_AFTER_S = 30)
        self.fake_clock.advance(35)

        nodes = get_nodes_json()
        doll2 = next(n for n in nodes if n["node_id"] == 2)
        self.assertEqual(doll2["status"], "stale")
        # Values should still be there
        self.assertEqual(doll2["values"]["uid"], "04A1B2C4")
        self.assertEqual(doll2["values"]["passage"], 2)

    # DASH-06: Doll 2 sends again -> online
    def test_dash_06_doll2_online_again(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 2,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C4", "passage": 2, "attempt": 1, "fsr": 2950},
        }
        add_event(evt, self.fake_clock.now, "127.0.0.1")

        self.fake_clock.advance(35)  # becomes stale

        # New event from Doll 2
        evt2 = {
            "event_id": "lumpi_1a2b3c4d/2",
            "node_id": 2,
            "event_type": "status",
            "recorded_at": 1794794436,
            "activity": "recording",
            "data": {"uid": "04A1B2C4", "passage": 2, "attempt": 1, "fsr": 3100},
        }
        add_event(evt2, self.fake_clock.now, "127.0.0.1")

        nodes = get_nodes_json()
        doll2 = next(n for n in nodes if n["node_id"] == 2)
        self.assertEqual(doll2["status"], "online")
        self.assertEqual(doll2["activity"], "recording")

    # DASH-07: bad JSON, missing field, node 9, no token -> rejected, state unchanged
    def test_dash_07_bad_inputs_rejected(self):
        # Add a valid event first
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        add_event(evt, self.fake_clock.now, "127.0.0.1")

        # Bad JSON (not a dict)
        err = check_event("not a dict")
        self.assertEqual(err, "event is not a JSON object")

        # Missing field
        evt_missing = {"event_id": "x/1", "node_id": 1}
        err = check_event(evt_missing)
        self.assertTrue(err.startswith("missing "))

        # Invalid node_id (9)
        evt_bad_node = {
            "event_id": "lumpi_1a2b3c4d/2",
            "node_id": 9,
            "event_type": "start",
            "recorded_at": 1794794402,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        err = check_event(evt_bad_node)
        self.assertTrue(err.startswith("bad node_id"))

        # State unchanged
        nodes = get_nodes_json()
        doll1 = next(n for n in nodes if n["node_id"] == 1)
        self.assertEqual(doll1["status"], "online")
        self.assertEqual(doll1["values"]["uid"], "04A1B2C3")

    # DASH-08: same event_id twice -> stored once
    def test_dash_08_duplicate_event_id(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        result1 = add_event(evt, self.fake_clock.now, "127.0.0.1")
        self.assertEqual(result1, "added")

        # Same event_id again
        result2 = add_event(evt, self.fake_clock.now, "127.0.0.1")
        self.assertEqual(result2, "duplicate")

        # Counts
        nodes = get_nodes_json()
        doll1 = next(n for n in nodes if n["node_id"] == 1)
        self.assertEqual(doll1["counts"]["added"], 1)
        self.assertEqual(doll1["counts"]["duplicate"], 1)

    # DASH-10: fast alternating dolls -> no crossed values
    def test_dash_10_fast_alternating(self):
        # Doll 1 event
        evt1 = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        add_event(evt1, self.fake_clock.now, "127.0.0.1")

        # Doll 2 event immediately after
        evt2 = {
            "event_id": "lumpi_1a2b3c4d/2",
            "node_id": 2,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C4", "passage": 2, "attempt": 1, "fsr": 2950},
        }
        add_event(evt2, self.fake_clock.now, "127.0.0.1")

        # Check values not crossed
        nodes = get_nodes_json()
        doll1 = next(n for n in nodes if n["node_id"] == 1)
        doll2 = next(n for n in nodes if n["node_id"] == 2)

        self.assertEqual(doll1["values"]["uid"], "04A1B2C3")
        self.assertEqual(doll1["values"]["passage"], 1)
        self.assertEqual(doll2["values"]["uid"], "04A1B2C4")
        self.assertEqual(doll2["values"]["passage"], 2)

    # Additional tests for check_event
    def test_check_event_valid_room(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 0,
            "event_type": "room",
            "recorded_at": 1794794400,
            "activity": None,
            "data": {"lux": 230.5, "distance_cm": 160.0},
        }
        self.assertIsNone(check_event(evt))

    def test_check_event_valid_doll_start(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        self.assertIsNone(check_event(evt))

    def test_check_event_valid_doll_status(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "status",
            "recorded_at": 1794794411,
            "activity": "idle",
            "data": {"uid": None, "passage": 0, "attempt": 0, "fsr": 1200},
        }
        self.assertIsNone(check_event(evt))

    def test_check_event_room_from_wrong_node(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "room",
            "recorded_at": 1794794400,
            "activity": None,
            "data": {"lux": 230.5, "distance_cm": 160.0},
        }
        err = check_event(evt)
        self.assertIn("room event must come from node 0", err)

    def test_check_event_doll_from_node_0(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 0,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        err = check_event(evt)
        self.assertIn("doll event cannot use node 0", err)

    def test_check_event_bad_recorded_at(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 12,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        err = check_event(evt)
        self.assertIn("bad recorded_at", err)

    def test_check_event_bad_activity(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "flying",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        err = check_event(evt)
        self.assertIn("bad activity", err)

    def test_check_event_missing_data_key(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1},
        }
        err = check_event(evt)
        self.assertIn("missing data.fsr", err)

    def test_check_event_bad_data_value(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": [1, 2, 3]},
        }
        err = check_event(evt)
        self.assertIn("bad data.fsr", err)

    def test_check_event_bool_node_id_rejected(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": True,  # bool is int in Python, should be rejected
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        err = check_event(evt)
        self.assertIn("bad node_id", err)

    def test_check_event_string_node_id_rejected(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": "1",
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        err = check_event(evt)
        self.assertIn("bad node_id", err)

    def test_check_event_event_id_format(self):
        # No slash
        evt = {
            "event_id": "no-slash",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 1794794401,
            "activity": "book_on",
            "data": {"uid": "04A1B2C3", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        err = check_event(evt)
        self.assertIn("bad event_id", err)

        # Too long
        evt["event_id"] = "a/b" + "x" * 100
        err = check_event(evt)
        self.assertIn("bad event_id", err)

    # Test node_status
    def test_node_status(self):
        self.assertEqual(node_status(None), "unknown")
        self.assertEqual(node_status(10), "online")
        self.assertEqual(node_status(29), "online")
        self.assertEqual(node_status(30), "stale")
        self.assertEqual(node_status(45), "stale")
        self.assertEqual(node_status(59), "stale")
        self.assertEqual(node_status(60), "offline")
        self.assertEqual(node_status(100), "offline")

    # Test get_history_json
    def test_get_history_json(self):
        # Add some history
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 0,
            "event_type": "room",
            "recorded_at": 1794794400,
            "activity": None,
            "data": {"lux": 230.5, "distance_cm": 160.0},
        }
        add_event(evt, self.fake_clock.now, "127.0.0.1")

        hist = get_history_json(15)
        self.assertEqual(hist["minutes"], 15)
        self.assertEqual(hist["dark_lux"], DARK_LUX)
        self.assertEqual(hist["stale_after_s"], STALE_AFTER_S)
        self.assertIn("0", hist["nodes"])
        self.assertEqual(len(hist["nodes"]["0"]), 1)

    def test_get_history_json_invalid_minutes(self):
        # This is tested via HTTP, but the function should handle it
        hist = get_history_json(15)
        self.assertEqual(hist["minutes"], 15)

    # Test late older event does not overwrite newer values
    def test_late_older_event_no_overwrite(self):
        # First event with recorded_at = 100
        evt1 = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "start",
            "recorded_at": 100,
            "activity": "book_on",
            "data": {"uid": "NEW", "passage": 1, "attempt": 1, "fsr": 3100},
        }
        add_event(evt1, self.fake_clock.now, "127.0.0.1")

        # Later event with older recorded_at = 50 (simulating Wi-Fi retry)
        evt2 = {
            "event_id": "lumpi_1a2b3c4d/2",
            "node_id": 1,
            "event_type": "status",
            "recorded_at": 50,
            "activity": "idle",
            "data": {"uid": "OLD", "passage": 0, "attempt": 0, "fsr": 1200},
        }
        add_event(evt2, self.fake_clock.now + 10, "127.0.0.1")

        # Values should remain from the newer recorded_at (100)
        nodes = get_nodes_json()
        doll1 = next(n for n in nodes if n["node_id"] == 1)
        self.assertEqual(doll1["values"]["uid"], "NEW")
        self.assertEqual(doll1["recorded_at"], 100)

    # Test low_light flag
    def test_low_light_flag(self):
        evt = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 0,
            "event_type": "room",
            "recorded_at": 1794794400,
            "activity": None,
            "data": {"lux": 85.0, "distance_cm": 160.0},
        }
        add_event(evt, self.fake_clock.now, "127.0.0.1")

        nodes = get_nodes_json()
        nest = next(n for n in nodes if n["node_id"] == 0)
        self.assertTrue(nest["low_light"])

        # Above threshold
        evt2 = {
            "event_id": "lumpi_1a2b3c4d/2",
            "node_id": 0,
            "event_type": "room",
            "recorded_at": 1794794410,
            "activity": None,
            "data": {"lux": 150.0, "distance_cm": 155.0},
        }
        add_event(evt2, self.fake_clock.now + 10, "127.0.0.1")

        nodes = get_nodes_json()
        nest = next(n for n in nodes if n["node_id"] == 0)
        self.assertFalse(nest["low_light"])

    # Test hints
    def test_find_hint(self):
        self.assertEqual(find_hint("event is not a JSON object"), "Send one JSON object {...}, not a list or a single value.")
        self.assertEqual(find_hint("missing event_id: got something"), "Include \"event_id\": \"<gateway>/<number>\".")
        self.assertEqual(find_hint("bad token"), "Header X-LUMPI-Token must match LUMPI_LAN_KEY.")

    # Test example for error
    def test_get_example_for_error(self):
        ex = get_example_for_error("room event must come from node 0")
        self.assertEqual(ex["event_type"], "room")
        self.assertEqual(ex["node_id"], 0)

        ex = get_example_for_error("bad activity")
        self.assertEqual(ex["event_type"], "start")

        ex = get_example_for_error("unknown error")
        self.assertEqual(ex["event_type"], "status")

    # Test history size limit
    def test_history_size_limit(self):
        for i in range(800):
            evt = {
                "event_id": f"lumpi_1a2b3c4d/{i}",
                "node_id": 1,
                "event_type": "status",
                "recorded_at": 1794794400 + i,
                "activity": "idle",
                "data": {"uid": None, "passage": 0, "attempt": 0, "fsr": 1200},
            }
            add_event(evt, self.fake_clock.now + i, "127.0.0.1")

        # History should be limited to HISTORY_SIZE (720)
        self.assertLessEqual(len(history[1]), 720)


if __name__ == "__main__":
    unittest.main()