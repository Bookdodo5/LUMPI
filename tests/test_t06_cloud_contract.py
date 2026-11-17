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

            payload = subprocess.check_output([str(executable)], text=True)
            event = json.loads(payload)

            saved_id = commit_event(
                database_path,
                "lumpi/lumpi_1a2b3c4d/events",
                payload,
            )

            self.assertEqual(saved_id, event["id"])

            with sqlite3.connect(database_path) as database:
                saved = database.execute(
                    "select id, body from events where id=?",
                    (saved_id,),
                ).fetchone()

            self.assertIsNotNone(saved)
            self.assertEqual(saved[0], "lumpi_1a2b3c4d/482913/7")
            saved_body = json.loads(saved[1])
            self.assertEqual(saved_body["kind"], "room")
            self.assertEqual(saved_body["recorded_at"], 1700000001)
            self.assertEqual(saved_body["lux"], 230.5)


if __name__ == "__main__":
    unittest.main()
