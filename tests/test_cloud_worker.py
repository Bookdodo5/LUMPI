import json
import os
import runpy
import sqlite3
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

from adapter.cloud_worker import (
    commit_event,
    main,
    on_connect,
    on_connect_fail,
    on_disconnect,
    on_message,
    run_network_loop,
)


class FakeClient:
    def __init__(self):
        self.subscriptions = []
        self.reconnect_delay = None
        self.connection = None
        self.retry_first_connection = None
        self.username_password = None
        self.tls = None

    def subscribe(self, topic, qos):
        self.subscriptions.append((topic, qos))

    def reconnect_delay_set(self, min_delay, max_delay):
        self.reconnect_delay = (min_delay, max_delay)

    def connect_async(self, host, port, keepalive):
        self.connection = (host, port, keepalive)

    def loop_forever(self, retry_first_connection):
        self.retry_first_connection = retry_first_connection

    def username_pw_set(self, username, password):
        self.username_password = (username, password)

    def tls_set(self, ca_certs, cert_reqs):
        self.tls = (ca_certs, cert_reqs)


class FailedReason:
    is_failure = True

    def __str__(self):
        return "test failure"


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

            self.assertIsNone(
                commit_event(database_path, "wrong/topic", json.dumps(good))
            )
            self.assertIsNone(commit_event(database_path, None, json.dumps(good)))
            self.assertIsNone(
                commit_event(database_path, "lumpi//events", json.dumps(good))
            )
            self.assertIsNone(
                commit_event(
                    database_path, "lumpi/gateway/events/extra", json.dumps(good)
                )
            )
            self.assertIsNone(commit_event(database_path, topic, "not json"))
            self.assertIsNone(commit_event(database_path, topic, b"x" * 2049))
            self.assertIsNone(commit_event(database_path, topic, "x" * 2049))
            self.assertIsNone(commit_event(database_path, topic, b"\xff"))
            self.assertIsNone(commit_event(database_path, topic, object()))
            self.assertIsNone(commit_event(database_path, topic, "[]"))
            self.assertIsNone(commit_event(database_path, topic, '{"id":"x"}'))
            self.assertIsNone(commit_event(database_path, topic, '{"id":1,"body":{}}'))
            self.assertIsNone(
                commit_event(database_path, topic, '{"id":"x","body":[]}')
            )

            long_identity = "lumpi_1a2b3c4d/" + "x" * (96 - len("lumpi_1a2b3c4d/"))
            too_long = {"id": long_identity, "body": {}}
            self.assertIsNone(commit_event(database_path, topic, json.dumps(too_long)))

            not_json_number = {
                "id": "lumpi_1a2b3c4d/1/2",
                "body": {"lux": float("nan")},
            }
            self.assertIsNone(
                commit_event(database_path, topic, json.dumps(not_json_number))
            )

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

    def test_failed_connection_does_not_subscribe(self):
        client = FakeClient()

        on_connect(client, None, None, FailedReason(), None)

        self.assertEqual(client.subscriptions, [])

    def test_initial_failure_and_reconnect_are_retried(self):
        client = FakeClient()

        run_network_loop(client, "broker.example")

        self.assertEqual(client.reconnect_delay, (1, 30))
        self.assertEqual(client.connection, ("broker.example", 8883, 60))
        self.assertTrue(client.retry_first_connection)

    def test_connection_and_message_logs(self):
        with tempfile.TemporaryDirectory() as folder:
            database_path = Path(folder) / "cloud.sqlite"
            good_message = types.SimpleNamespace(
                topic="lumpi/lumpi_1a2b3c4d/events",
                payload=json.dumps(
                    {
                        "id": "lumpi_1a2b3c4d/1/1",
                        "body": {"kind": "room", "recorded_at": 1700000001, "lux": 1},
                    }
                ),
            )
            bad_message = types.SimpleNamespace(
                topic="lumpi/lumpi_1a2b3c4d/events",
                payload="bad",
            )

            with patch("builtins.print") as print_mock:
                on_connect_fail(None, None)
                on_disconnect(None, None, None, "test outage", None)
                on_message(None, {"database": database_path}, good_message)
                on_message(None, {"database": database_path}, bad_message)

            printed = [call.args[0] for call in print_mock.call_args_list]
            self.assertIn("cloud connection failed; retrying", printed)
            self.assertIn("cloud disconnected: test outage; retrying", printed)
            self.assertIn("saved lumpi_1a2b3c4d/1/1", printed)
            self.assertIn(
                "ignored a bad message on lumpi/lumpi_1a2b3c4d/events",
                printed,
            )

    def test_main_configures_secure_authenticated_client(self):
        with tempfile.TemporaryDirectory() as folder:
            database_path = Path(folder) / "nested" / "cloud.sqlite"
            fake_client = FakeClient()
            fake_mqtt_client = types.SimpleNamespace(
                CallbackAPIVersion=types.SimpleNamespace(VERSION2=2),
                Client=lambda *args, **kwargs: fake_client,
            )
            fake_mqtt_package = types.ModuleType("paho.mqtt")
            fake_mqtt_package.client = fake_mqtt_client
            fake_paho_package = types.ModuleType("paho")
            fake_paho_package.mqtt = fake_mqtt_package

            environment = {
                "LUMPI_MQTT_HOST": "broker.example",
                "LUMPI_INGEST_USER": "ingest-user",
                "LUMPI_INGEST_PASSWORD": "test-only-password",
                "LUMPI_CLOUD_DB": str(database_path),
                "LUMPI_MQTT_CA_FILE": "/tmp/test-ca.pem",
            }
            modules = {
                "paho": fake_paho_package,
                "paho.mqtt": fake_mqtt_package,
            }

            with (
                patch.dict(os.environ, environment, clear=True),
                patch.dict(sys.modules, modules),
                patch("adapter.cloud_worker.run_network_loop") as loop_mock,
            ):
                main()

            self.assertTrue(database_path.parent.is_dir())
            self.assertEqual(
                fake_client.username_password,
                ("ingest-user", "test-only-password"),
            )
            self.assertEqual(fake_client.tls[0], "/tmp/test-ca.pem")
            self.assertIs(fake_client.on_connect, on_connect)
            self.assertIs(fake_client.on_connect_fail, on_connect_fail)
            self.assertIs(fake_client.on_disconnect, on_disconnect)
            self.assertIs(fake_client.on_message, on_message)
            loop_mock.assert_called_once_with(fake_client, "broker.example")

    def test_script_entrypoint_runs_main(self):
        with tempfile.TemporaryDirectory() as folder:
            fake_client = FakeClient()
            fake_mqtt_client = types.SimpleNamespace(
                CallbackAPIVersion=types.SimpleNamespace(VERSION2=2),
                Client=lambda *args, **kwargs: fake_client,
            )
            fake_mqtt_package = types.ModuleType("paho.mqtt")
            fake_mqtt_package.client = fake_mqtt_client
            fake_paho_package = types.ModuleType("paho")
            fake_paho_package.mqtt = fake_mqtt_package

            environment = {
                "LUMPI_MQTT_HOST": "broker.example",
                "LUMPI_INGEST_USER": "ingest-user",
                "LUMPI_INGEST_PASSWORD": "test-only-password",
                "LUMPI_CLOUD_DB": str(Path(folder) / "cloud.sqlite"),
            }
            modules = {
                "paho": fake_paho_package,
                "paho.mqtt": fake_mqtt_package,
            }

            with (
                patch.dict(os.environ, environment, clear=True),
                patch.dict(sys.modules, modules),
            ):
                worker_path = (
                    Path(__file__).resolve().parents[1] / "adapter/cloud_worker.py"
                )
                runpy.run_path(str(worker_path), run_name="__main__")

            self.assertEqual(fake_client.connection, ("broker.example", 8883, 60))
            self.assertTrue(fake_client.retry_first_connection)


if __name__ == "__main__":
    unittest.main()
