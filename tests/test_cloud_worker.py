import json
import sqlite3
import tempfile
import unittest
from pathlib import Path

from adapter.cloud_worker import commit_event, on_connect


class FakeClient:
    def __init__(self):
        self.subscriptions = []

    def subscribe(self, topic, qos):
        self.subscriptions.append((topic, qos))


class CloudWorkerTest(unittest.TestCase):
    def test_store_retrieve_and_deduplicate(self):
        with tempfile.TemporaryDirectory() as folder:
            database_path = Path(folder) / "cloud.sqlite"
            event = {
                "id": "lumpi_1a2b3c4d/482913/7",
                "body": {
                    "kind": "room",
                    "recorded_at": 1700000001,
                    "lux": 230.5,
                },
            }
            payload = json.dumps(event)

            identity = commit_event(
                database_path,
                "lumpi/lumpi_1a2b3c4d/events",
                payload,
            )
            self.assertEqual(identity, event["id"])

            duplicate = commit_event(
                database_path,
                "lumpi/lumpi_1a2b3c4d/events",
                payload,
            )
            self.assertEqual(duplicate, event["id"])

            with sqlite3.connect(database_path) as database:
                rows = database.execute("select id, body from events").fetchall()

            self.assertEqual(len(rows), 1)
            saved_body = json.loads(rows[0][1])
            self.assertEqual(saved_body["recorded_at"], 1700000001)
            self.assertEqual(saved_body["lux"], 230.5)

    def test_reject_bad_messages(self):
        with tempfile.TemporaryDirectory() as folder:
            database_path = Path(folder) / "cloud.sqlite"
            topic = "lumpi/lumpi_1a2b3c4d/events"
            good = {
                "id": "lumpi_1a2b3c4d/1/1",
                "body": {"kind": "room", "recorded_at": 1700000001, "lux": 100},
            }

            self.assertIsNone(commit_event(database_path, "wrong/topic", json.dumps(good)))
            self.assertIsNone(commit_event(database_path, topic, "not json"))
            self.assertIsNone(commit_event(database_path, topic, b"x" * 2049))

            wrong_gateway = dict(good)
            wrong_gateway["id"] = "another_gateway/1/1"
            self.assertIsNone(
                commit_event(database_path, topic, json.dumps(wrong_gateway))
            )

            self.assertEqual(
                commit_event(database_path, topic, json.dumps(good)),
                good["id"],
            )

            changed = dict(good)
            changed["body"] = {"kind": "room", "recorded_at": 1700000001, "lux": 999}
            self.assertIsNone(commit_event(database_path, topic, json.dumps(changed)))

    def test_subscribe_again_after_reconnect(self):
        client = FakeClient()

        on_connect(client, None, None, 0, None)
        on_connect(client, None, None, 0, None)

        self.assertEqual(
            client.subscriptions,
            [("lumpi/+/events", 1), ("lumpi/+/events", 1)],
        )


if __name__ == "__main__":
    unittest.main()
