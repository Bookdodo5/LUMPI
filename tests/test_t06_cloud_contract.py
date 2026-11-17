import json
import sqlite3
import subprocess
import tempfile
import unittest
from pathlib import Path

from adapter.cloud_worker import commit_event


class T06CloudContractTest(unittest.TestCase):
    def test_real_firmware_payload_reaches_storage(self):
        project = Path(__file__).resolve().parents[1]

        with tempfile.TemporaryDirectory() as folder:
            executable = Path(folder) / "emit_gateway_cloud_event"
            database_path = Path(folder) / "cloud.sqlite"

            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(project / "src/common"),
                    str(project / "tests/emit_gateway_cloud_event.cpp"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )

            output = subprocess.check_output([str(executable)], text=True)
            payloads = output.splitlines()
            self.assertEqual(len(payloads), 2)

            for payload in payloads:
                event = json.loads(payload)
                saved_id = commit_event(
                    database_path,
                    "lumpi/lumpi_1a2b3c4d/events",
                    payload,
                )
                self.assertEqual(saved_id, event["id"])

            with sqlite3.connect(database_path) as database:
                saved_rows = database.execute(
                    "select id, body from events order by id",
                ).fetchall()

            self.assertEqual(len(saved_rows), 2)
            saved = {identity: json.loads(body) for identity, body in saved_rows}

            room = saved["lumpi_1a2b3c4d/482913/7"]
            self.assertEqual(room["kind"], "room")
            self.assertEqual(room["recorded_at"], 1700000001)
            self.assertEqual(room["lux"], 230.5)

            station = saved["lumpi_1a2b3c4d/482913/8"]
            self.assertEqual(station["kind"], "recording_start")
            self.assertEqual(station["node"], 2)
            self.assertEqual(station["attempt"], 4)
            self.assertEqual(station["book"], "04A1B2C3")
            self.assertEqual(station["passage"], 3)
            self.assertEqual(station["fsr"], 3100)


if __name__ == "__main__":
    unittest.main()
