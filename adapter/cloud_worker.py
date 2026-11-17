"""Receive LUMPI MQTT events and store each unique event in SQLite."""

import json
import os
import sqlite3
import ssl
from pathlib import Path


MAX_PAYLOAD_BYTES = 2048
EVENT_TOPIC = "lumpi/+/events"


def gateway_from_topic(topic):
    parts = topic.split("/")
    if len(parts) != 3:
        return None
    if parts[0] != "lumpi" or not parts[1] or parts[2] != "events":
        return None
    return parts[1]


def commit_event(database_path, topic, payload):
    """Validate one MQTT event, save it once, and return its id."""
    try:
        if isinstance(payload, bytes):
            if len(payload) > MAX_PAYLOAD_BYTES:
                return None
            payload = payload.decode("utf-8")
        elif isinstance(payload, str):
            if len(payload.encode("utf-8")) > MAX_PAYLOAD_BYTES:
                return None
        else:
            return None

        gateway_id = gateway_from_topic(topic)
        if gateway_id is None:
            return None

        event = json.loads(payload)
        if not isinstance(event, dict):
            return None

        identity = event["id"]
        body = event["body"]

        if not isinstance(identity, str) or not isinstance(body, dict):
            return None
        if len(identity) >= 96 or not identity.startswith(gateway_id + "/"):
            return None

        body_text = json.dumps(
            body,
            sort_keys=True,
            allow_nan=False,
            separators=(",", ":"),
        )

        with sqlite3.connect(database_path) as database:
            database.execute(
                "create table if not exists events("
                "id text primary key, body text not null)"
            )

            old = database.execute(
                "select body from events where id=?",
                (identity,),
            ).fetchone()

            if old is not None and old[0] != body_text:
                return None

            database.execute(
                "insert or ignore into events values(?, ?)",
                (identity, body_text),
            )
            database.commit()

        return identity
    except (json.JSONDecodeError, KeyError, TypeError, UnicodeError, ValueError, sqlite3.Error):
        return None


def on_connect(client, _userdata, _flags, reason_code, _properties):
    failed = getattr(reason_code, "is_failure", reason_code != 0)
    if failed:
        print(f"cloud connection failed: {reason_code}")
        return

    client.subscribe(EVENT_TOPIC, qos=1)
    print("cloud connected and subscribed")


def on_connect_fail(_client, _userdata):
    print("cloud connection failed; retrying")


def on_disconnect(_client, _userdata, _flags, reason_code, _properties):
    print(f"cloud disconnected: {reason_code}; retrying")


def on_message(_client, userdata, message):
    identity = commit_event(
        userdata["database"],
        message.topic,
        message.payload,
    )

    if identity is None:
        print(f"ignored a bad message on {message.topic}")
    else:
        print(f"saved {identity}")


def run_network_loop(client, host):
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    client.connect_async(host, 8883, 60)
    client.loop_forever(retry_first_connection=True)


def main():
    from paho.mqtt import client as mqtt

    host = os.environ["LUMPI_MQTT_HOST"]
    username = os.environ["LUMPI_INGEST_USER"]
    password = os.environ["LUMPI_INGEST_PASSWORD"]
    database_path = Path(os.environ.get("LUMPI_CLOUD_DB", "build/cloud.sqlite"))
    ca_file = os.environ.get("LUMPI_MQTT_CA_FILE")

    database_path.parent.mkdir(parents=True, exist_ok=True)

    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id="lumpi_cloud_ingest",
        userdata={"database": database_path},
    )
    client.username_pw_set(username, password)
    client.tls_set(ca_certs=ca_file, cert_reqs=ssl.CERT_REQUIRED)
    client.on_connect = on_connect
    client.on_connect_fail = on_connect_fail
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    run_network_loop(client, host)


if __name__ == "__main__":
    main()
