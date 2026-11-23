"""Send a scenario file to the running dashboard server, with its real delays.

Run from the project folder, with the server already running:
    LUMPI_LAN_KEY=test python3 laptop/replay_events.py tests/fixtures/dashboard/normal.jsonl

Each line of the file is one of:
    {"delay": 1.5, "event": {...}, "expect": "added"}
    {"delay": 0.2, "raw": "broken text", "expect": "rejected"}
"expect" is optional. A file saved with dashboard_server.py --record works too.

Event IDs and times are moved to "now", so the same file can be replayed
again without restarting the server, and the page shows current times.
Duplicates inside one file stay duplicates.
"""

import json
import os
import sys
import time
import urllib.error
import urllib.request


# LUMPI_URL=https://<your-site>.vercel.app sends to the cloud instead of the laptop.
SERVER = os.environ.get("LUMPI_URL", "http://localhost:8080") + "/api/telemetry"


def send(body_text, token):
    request = urllib.request.Request(
        SERVER,
        data=body_text.encode("utf-8"),
        headers={"X-LUMPI-Token": token, "Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request) as response:
            return json.loads(response.read())["result"]
    except urllib.error.HTTPError as error:
        return f"rejected ({error.code} {json.loads(error.read())['error']})"


def main():
    if len(sys.argv) != 2:
        print("usage: python3 laptop/replay_events.py <scenario.jsonl>")
        return

    token = os.environ.get("LUMPI_LAN_KEY", "")
    if not token:
        print("Set LUMPI_LAN_KEY first.")
        return

    with open(sys.argv[1]) as file:
        lines = [json.loads(text) for text in file if text.strip()]

    run = int(time.time())
    first_recorded = None
    mismatches = 0

    for line in lines:
        time.sleep(line["delay"])

        if "raw" in line:
            body_text = line["raw"]
            label = "raw"
        else:
            event = dict(line["event"])
            if first_recorded is None:
                first_recorded = event["recorded_at"]
            event["recorded_at"] = run + (event["recorded_at"] - first_recorded)
            gateway, number = event["event_id"].split("/", 1)
            event["event_id"] = f"{gateway}/{run}-{number}"
            body_text = json.dumps(event)
            label = f"node {event['node_id']} {event['event_type']}"

        try:
            result = send(body_text, token)
        except urllib.error.URLError as error:
            print(f"cannot reach the server: {error.reason}")
            return

        expect = line.get("expect")
        note = ""
        if expect is not None and not result.startswith(expect):
            note = f"   <-- expected {expect}"
            mismatches += 1

        print(f"{label:<16} {result}{note}")

    print(f"done, {len(lines)} lines, {mismatches} not as expected")


if __name__ == "__main__":
    main()
