import json
import sqlite3
import tempfile
import unittest
from pathlib import Path

from adapter.cloud_worker import commit_event, on_connect, run_network_loop


class FakeClient:
    def __init__(self):
        self.subscriptions = []
        self.reconnect_delay = None
        self.connection = None
        self.retry_first_connection = None

    def subscribe(self, topic, qos):
        self.subscriptions.append((topic, qos))

    def reconnect_delay_set(self, min_delay, max_delay):
        self.reconnect_delay = (min_delay, max_delay)

    def connect_async(self, host, port, keepalive):
        self.connection = (host, port, keepalive)

    def loop_forever(self, retry_first_connection):
        self.retry_first_connection = retry_first_connection


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

    def test_initial_failure_and_reconnect_are_retried(self):
        client = FakeClient()

        run_network_loop(client, "broker.example")

        self.assertEqual(client.reconnect_delay, (1, 30))
        self.assertEqual(client.connection, ("broker.example", 8883, 60))
        self.assertTrue(client.retry_first_connection)


if __name__ == "__main__":
    unittest.main()
