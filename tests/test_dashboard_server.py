"""Dashboard server tests.

Run from the project folder:
    python3 -m unittest tests.test_dashboard_server

Each scenario file in tests/fixtures/dashboard is played through the same
function the server uses for POST /api/telemetry, with a fake clock, so the
tests do not wait in real time.
"""

import json
import shutil
import socket
import subprocess
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from laptop import dashboard_server as server


FIXTURES = Path(__file__).resolve().parent / "fixtures" / "dashboard"
START = 1000.0


def play(name):
    """Play one scenario (a fixture name or a file path). Return the fake clock time after the last line."""
    now = START
    if isinstance(name, Path):
        path = name
    else:
        path = FIXTURES / f"{name}.jsonl"

    for number, text in enumerate(path.read_text().splitlines(), start=1):
        line = json.loads(text)
        now += line["delay"]

        if "raw" in line:
            body = line["raw"]
        else:
            body = json.dumps(line["event"])

        code, message = server.handle_telemetry(body, now)

        if code == 200:
            result = message
        else:
            result = "rejected"

        if result != line["expect"]:
            raise AssertionError(f"{name} line {number}: got {result} ({message}), expected {line['expect']}")

    return now


def view(now):
    """Node list as a dict keyed by node_id."""
    result = {}
    for node in server.nodes_view(now):
        result[node["node_id"]] = node
    return result


class ScenarioTest(unittest.TestCase):
    def setUp(self):
        server.clear_state()

    def test_empty_shows_unknown(self):
        nodes = view(START)
        self.assertEqual(sorted(nodes), [0, 1, 2])
        for node in nodes.values():
            self.assertEqual(node["status"], "unknown")

    def test_normal(self):
        end = play("normal")
        nodes = view(end)

        for node_id in [0, 1, 2]:
            self.assertEqual(nodes[node_id]["status"], "online")

        self.assertEqual(nodes[1]["values"]["uid"], "04A1B2C3")
        self.assertEqual(nodes[2]["values"]["uid"], "04D5E6F7")
        self.assertEqual(nodes[1]["values"]["passage"], 1)
        self.assertEqual(nodes[2]["values"]["passage"], 2)
        self.assertIsNotNone(nodes[0]["values"]["lux"])
        self.assertEqual(len(server.recent), server.RECENT_SIZE)

    def test_book_removed_clears_old_book(self):
        # Found in the live replay: an idle doll kept showing the old book.
        end = play("normal")
        idle = {
            "event_id": "lumpi_1a2b3c4d/500",
            "node_id": 2,
            "event_type": "status",
            "recorded_at": 1794794400 + 70,
            "activity": "idle",
            "data": {"uid": None, "passage": None, "attempt": None, "fsr": 0},
        }
        code, _ = server.handle_telemetry(json.dumps(idle), end + 1)
        self.assertEqual(code, 200)

        values = view(end + 1)[2]["values"]
        self.assertIsNone(values["uid"])
        self.assertIsNone(values["passage"])

    def test_teacher_view(self):
        # Mock data for the v7 teacher page: every activity, every light zone, a failed sensor.
        end = play("teacher_view")
        nodes = view(end)

        # The late quiet_study message did not overwrite doll 1.
        self.assertEqual(nodes[1]["activity"], "recording")
        self.assertEqual(nodes[1]["values"]["attempt"], 2)
        self.assertEqual(nodes[1]["counts"], {"added": 7, "duplicate": 1})
        self.assertEqual(nodes[2]["activity"], "stopped")
        self.assertEqual(nodes[0]["values"], {"lux": 420.0, "distance_cm": 320.0})

        # The late message is still kept in history, for the activity list.
        activities = [point["activity"] for point in server.history[1]]
        self.assertIn("quiet_study", activities)

    def test_doll2_dies(self):
        end = play("doll2_dies")
        doll2_last = server.latest[2]["received_at"]

        self.assertEqual(view(doll2_last + 29)[2]["status"], "online")
        self.assertEqual(view(doll2_last + 31)[2]["status"], "stale")
        self.assertEqual(view(doll2_last + 61)[2]["status"], "offline")

        nodes = view(end)
        self.assertEqual(nodes[2]["status"], "offline")
        self.assertEqual(nodes[1]["status"], "online")
        self.assertEqual(nodes[0]["status"], "online")
        # Last known values stay visible.
        self.assertEqual(nodes[2]["values"]["uid"], "04D5E6F7")

    def test_flaky_wifi(self):
        end = play("flaky_wifi")
        nodes = view(end)

        ids = [event["event_id"] for event in server.recent]
        self.assertEqual(len(ids), len(set(ids)))

        # Doll 1 came back after its 40 s gap.
        self.assertEqual(nodes[1]["status"], "online")

        # The late event (recorded at 18 s, fsr 1) must not replace newer values.
        self.assertNotEqual(nodes[1]["values"]["fsr"], 1)
        self.assertGreater(nodes[1]["recorded_at"], 1794794400 + 18)

    def test_sensor_fail(self):
        end = play("sensor_fail")
        nodes = view(end)

        self.assertIsNone(nodes[0]["values"]["lux"])
        self.assertIsNone(nodes[0]["values"]["distance_cm"])
        self.assertIsNone(nodes[1]["values"]["fsr"])

    def test_busy_dolls_do_not_cross(self):
        end = play("busy")
        nodes = view(end)

        self.assertEqual(nodes[1]["values"]["uid"], "04A1B2C3")
        self.assertEqual(nodes[1]["values"]["passage"], 1)
        self.assertEqual(nodes[2]["values"]["uid"], "04D5E6F7")
        self.assertEqual(nodes[2]["values"]["passage"], 3)

        for event in server.recent:
            if event["node_id"] == 1:
                self.assertEqual(event["data"]["uid"], "04A1B2C3")
            else:
                self.assertEqual(event["data"]["uid"], "04D5E6F7")

    def test_bad_packets_change_nothing(self):
        end = play("bad_packets")
        nodes = view(end)

        # Only the first and last valid events got through.
        self.assertEqual(len(server.recent), 2)
        self.assertEqual(nodes[1]["values"]["fsr"], 5)
        self.assertEqual(nodes[0]["status"], "unknown")
        self.assertEqual(nodes[2]["status"], "unknown")


def doll_event(number, node_id=1, recorded_at=1794794400):
    return {
        "event_id": f"lumpi_1a2b3c4d/{number}",
        "node_id": node_id,
        "event_type": "status",
        "recorded_at": recorded_at,
        "activity": "idle",
        "data": {"uid": None, "passage": None, "attempt": None, "fsr": 0},
    }


def room_event(number, lux):
    return {
        "event_id": f"lumpi_1a2b3c4d/{number}",
        "node_id": 0,
        "event_type": "room",
        "recorded_at": 1794794400 + number,
        "activity": None,
        "data": {"lux": lux, "distance_cm": 150.0},
    }


class DeveloperViewTest(unittest.TestCase):
    def setUp(self):
        server.clear_state()

    def test_counts_duplicates(self):
        play("flaky_wifi")
        duplicates = sum(count["duplicate"] for count in server.counts.values())
        self.assertEqual(duplicates, 3)
        for node_id in [0, 1, 2]:
            self.assertGreater(server.counts[node_id]["added"], 0)

    def test_every_rejected_message_has_a_hint(self):
        play("bad_packets")
        view_data = server.errors_view()

        self.assertEqual(view_data["rejected_total"], 20)
        for error in view_data["errors"]:
            self.assertNotEqual(error["hint"], "", error["reason"])
            self.assertIn("event_id", error["example"])

        # Newest first, and the raw packet is kept for the developer.
        ids = [error["id"] for error in view_data["errors"]]
        self.assertEqual(ids, sorted(ids, reverse=True))
        node9 = [error for error in view_data["errors"] if "got 9" in error["reason"]]
        self.assertEqual(len(node9), 1)
        self.assertIn('"node_id": 9', node9[0]["raw"])

    def test_error_list_is_bounded(self):
        for number in range(60):
            server.handle_telemetry("not json", START + number)
        self.assertEqual(len(server.errors), server.ERROR_SIZE)
        self.assertEqual(server.totals["rejected"], 60)

    def test_history_window(self):
        server.add_event(doll_event(1), START)
        server.add_event(doll_event(2), START + 400)

        five = server.history_view(START + 400, 5)["nodes"]["1"]
        fifteen = server.history_view(START + 400, 15)["nodes"]["1"]
        self.assertEqual(len(five), 1)
        self.assertEqual(len(fifteen), 2)
        self.assertEqual(server.history_view(START + 400, 5)["nodes"]["2"], [])

    def test_history_is_bounded(self):
        for number in range(800):
            server.add_event(doll_event(number), START + number)
        self.assertEqual(len(server.history[1]), server.HISTORY_SIZE)

    def test_duplicates_not_in_history(self):
        server.add_event(doll_event(1), START)
        server.add_event(doll_event(1), START + 1)
        self.assertEqual(len(server.history[1]), 1)

    def test_low_light(self):
        server.add_event(room_event(1, 50.0), START)
        self.assertTrue(view(START)[0]["low_light"])

        server.add_event(room_event(2, None), START + 10)
        self.assertFalse(view(START + 10)[0]["low_light"])

        server.add_event(room_event(3, 230.0), START + 20)
        self.assertFalse(view(START + 20)[0]["low_light"])


class RecordTest(unittest.TestCase):
    def setUp(self):
        server.clear_state()

    def tearDown(self):
        if server.recorder["file"] is not None:
            server.recorder["file"].close()
        server.recorder["file"] = None
        server.recorder["last_time"] = None

    def test_recording_replays_to_the_same_state(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "lesson.jsonl"
            server.recorder["file"] = open(path, "w", encoding="utf-8")

            end = play("bad_packets")
            before = view(end)
            server.recorder["file"].close()
            server.recorder["file"] = None

            lines = [json.loads(text) for text in path.read_text().splitlines()]
            self.assertEqual(len(lines), 22)
            self.assertEqual(sum(1 for line in lines if "raw" in line), 20)
            self.assertTrue(all("received_at" in line for line in lines))

            # A recording is a scenario file: replaying it gives the same result.
            server.clear_state()
            end = play(path)
            after = view(end)

        self.assertEqual(after[1]["values"], before[1]["values"])
        self.assertEqual(server.totals["rejected"], 20)


class FirmwareFormatTest(unittest.TestCase):
    """The nest's C++ event builder and this server must agree on the format."""

    def test_cpp_events_are_accepted(self):
        if shutil.which("g++") is None:
            self.skipTest("g++ not installed")

        root = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory() as folder:
            program = Path(folder) / "test_dashboard_event"
            subprocess.run(
                ["g++", "-std=c++17", "-I", str(root / "src/common"),
                 str(root / "tests/test_dashboard_event.cpp"), "-o", str(program)],
                check=True,
            )
            output = subprocess.run([str(program)], check=True, capture_output=True, text=True).stdout

        lines = output.splitlines()
        self.assertEqual(len(lines), 3)
        for text in lines:
            self.assertIsNone(server.check_event(json.loads(text)), text)


class StatusTest(unittest.TestCase):
    def test_thresholds(self):
        self.assertEqual(server.node_status(None), "unknown")
        self.assertEqual(server.node_status(0), "online")
        self.assertEqual(server.node_status(29.9), "online")
        self.assertEqual(server.node_status(30), "stale")
        self.assertEqual(server.node_status(59.9), "stale")
        self.assertEqual(server.node_status(60), "offline")


class HttpTest(unittest.TestCase):
    def setUp(self):
        server.clear_state()
        self.http = server.make_server(0, "test-token")
        self.port = self.http.server_address[1]
        self.thread = threading.Thread(target=self.http.serve_forever)
        self.thread.start()

    def tearDown(self):
        self.http.shutdown()
        self.http.server_close()
        self.thread.join()

    def post(self, body, token="test-token"):
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}/api/telemetry",
            data=body,
            headers={"X-LUMPI-Token": token, "Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request) as response:
                return response.status
        except urllib.error.HTTPError as error:
            return error.code

    def get(self, path):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}{path}", timeout=5) as response:
            return response.status, response.read()

    def test_post_and_read_back(self):
        event = {
            "event_id": "lumpi_1a2b3c4d/1",
            "node_id": 1,
            "event_type": "status",
            "recorded_at": 1794794400,
            "activity": "idle",
            "data": {"uid": None, "passage": None, "attempt": None, "fsr": 0},
        }
        body = json.dumps(event).encode("utf-8")

        self.assertEqual(self.post(body, token="wrong"), 401)
        self.assertEqual(self.post(body, token=""), 401)
        self.assertEqual(self.post(b"x" * (server.MAX_BODY_BYTES + 1)), 400)
        self.assertEqual(self.post(b"{not json"), 400)
        self.assertEqual(len(server.recent), 0)

        self.assertEqual(self.post(body), 200)

        status, text = self.get("/api/dashboard/nodes")
        nodes = {node["node_id"]: node for node in json.loads(text)}
        self.assertEqual(status, 200)
        self.assertEqual(nodes[1]["status"], "online")
        self.assertEqual(nodes[2]["status"], "unknown")

        status, text = self.get("/api/dashboard/events")
        self.assertEqual(len(json.loads(text)), 1)

    def test_history_and_errors_endpoints(self):
        self.assertEqual(self.post(b"{}", token="wrong"), 401)

        status, text = self.get("/api/dashboard/errors")
        errors = json.loads(text)
        self.assertEqual(errors["rejected_total"], 1)
        # A wrong-token body is never kept.
        self.assertEqual(errors["errors"][0]["raw"], "")

        status, text = self.get("/api/dashboard/history?minutes=60")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(text)["minutes"], 60)

        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.get("/api/dashboard/history?minutes=7")
        self.assertEqual(caught.exception.code, 400)

    def test_idle_connection_does_not_block(self):
        # Found in the v7 live test: Chrome opened a spare connection and sent
        # nothing, and the single-threaded server stopped answering everyone.
        idle = socket.create_connection(("127.0.0.1", self.port))
        try:
            status, _ = self.get("/api/dashboard/nodes")
            self.assertEqual(status, 200)
        finally:
            idle.close()

    def test_page_files_are_served(self):
        for path in ["/", "/doll.html", "/session.html", "/style.css", "/app.js", "/shared.js", "/doll.js", "/debug/", "/debug/charts.js"]:
            status, _ = self.get(path)
            self.assertEqual(status, 200)


if __name__ == "__main__":
    unittest.main()
