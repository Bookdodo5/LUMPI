#!/usr/bin/env python3
"""Plays a scenario file to the running dashboard server, with the file's real delays."""

import json
import os
import sys
import time
import urllib.error
import urllib.request

DEFAULT_URL = "http://localhost:8204"
VALID_CLOCK_AFTER = 1700000000


def make_unique(event, run):
    """Give each replay its own event IDs; duplicates inside one file stay duplicates."""
    sent = dict(event)
    if type(sent.get("recorded_at")) is int and sent["recorded_at"] > VALID_CLOCK_AFTER:
        sent["recorded_at"] = int(time.time())  # keeps "bad clock" fixtures bad
    event_id = sent.get("event_id")
    if isinstance(event_id, str) and "/" in event_id:
        gateway, number = event_id.split("/", 1)
        sent["event_id"] = f"{gateway}/{run}-{number}"
    return sent


def post(url, key, body):
    request = urllib.request.Request(
        url, data=body, method="POST",
        headers={"X-LUMPI-Token": key, "Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            payload = json.loads(response.read().decode("utf-8"))
            return response.status, payload.get("result", "added")
    except urllib.error.HTTPError as error:
        return error.code, "rejected"


def main():
    if len(sys.argv) != 2:
        print("usage: replay_events.py <scenario.jsonl>")
        return 2
    key = os.environ.get("LUMPI_LAN_KEY")
    if not key:
        print("LUMPI_LAN_KEY is not set")
        return 2
    url = os.environ.get("LUMPI_URL", DEFAULT_URL) + "/api/telemetry"
    run = str(int(time.time()))

    with open(sys.argv[1], encoding="utf-8") as scenario:
        lines = [json.loads(line) for line in scenario if line.strip()]

    mismatches = 0
    for number, line in enumerate(lines, start=1):
        time.sleep(line.get("delay", 0))
        if "event" in line:
            body = json.dumps(make_unique(line["event"], run)).encode("utf-8")
        else:
            body = line["raw"].encode("utf-8")
        try:
            status, result = post(url, key, body)
        except urllib.error.URLError as error:
            print(f"cannot reach the server at {url}: {error.reason}")
            return 1
        expect = line["expect"]
        ok = result == expect
        if not ok:
            mismatches += 1
        mark = "ok" if ok else "MISMATCH"
        print(f"{number:3} {status} {result:<9} expected {expect:<9} {mark}")

    print(f"{len(lines)} messages, {mismatches} not as expected")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
