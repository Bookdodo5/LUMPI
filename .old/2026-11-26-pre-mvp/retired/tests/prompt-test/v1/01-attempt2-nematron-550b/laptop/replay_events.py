#!/usr/bin/env python3
"""
Replay scenario events to the running dashboard server.

Reads a .jsonl scenario file and sends each event to localhost:8202
with real delays between messages.
"""

import sys
import json
import time
import uuid
import hmac
import hashlib
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 replay_events.py <scenario.jsonl>")
        sys.exit(1)

    scenario_file = sys.argv[1]
    lan_key = "test"  # matches BRIEF.md constraint

    # Generate a run ID for unique event IDs
    run_id = uuid.uuid4().hex[:8]

    try:
        with open(scenario_file, 'r') as f:
            lines = f.readlines()
    except FileNotFoundError:
        print(f"File not found: {scenario_file}")
        sys.exit(1)

    print(f"Replaying {scenario_file} (run {run_id}) to http://localhost:8202")

    unexpected = 0
    line_num = 0

    for line in lines:
        line_num += 1
        line = line.strip()
        if not line:
            continue

        try:
            entry = json.loads(line)
        except json.JSONDecodeError as e:
            print(f"Line {line_num}: invalid JSON: {e}")
            unexpected += 1
            continue

        delay = entry.get("delay", 0)
        if delay > 0:
            time.sleep(delay)

        if "event" in entry:
            # Good event - fix IDs and timestamps, use unique run prefix
            ev = entry["event"].copy()
            ev["event_id"] = f"lumpi_{run_id}/{line_num}"
            ev["recorded_at"] = int(time.time())
            expect = entry.get("expect", "added")

            body = json.dumps(ev).encode('utf-8')
            req = Request(
                "http://localhost:8202/api/telemetry",
                data=body,
                headers={
                    "Content-Type": "application/json",
                    "X-LUMPI-Token": lan_key,
                    "Content-Length": str(len(body))
                },
                method="POST"
            )

            try:
                with urlopen(req, timeout=5) as resp:
                    result = json.loads(resp.read().decode())
                    status = "OK" if result.get("result") == expect else f"UNEXPECTED (got {result.get('result')}, expected {expect})"
                    if "UNEXPECTED" in status:
                        unexpected += 1
                    print(f"  Line {line_num}: {status} - {ev['event_type']} node {ev['node_id']}")
            except HTTPError as e:
                err = json.loads(e.read().decode())
                status = "OK" if err.get("error") and expect == "rejected" else f"UNEXPECTED (HTTP {e.code}: {err})"
                if "UNEXPECTED" in status:
                    unexpected += 1
                print(f"  Line {line_num}: {status}")
            except URLError as e:
                print(f"  Line {line_num}: CONNECTION ERROR - {e}")
                unexpected += 1

        elif "raw" in entry:
            # Bad packet - send raw body
            raw = entry["raw"]
            expect = entry.get("expect", "rejected")

            req = Request(
                "http://localhost:8202/api/telemetry",
                data=raw.encode('utf-8'),
                headers={
                    "Content-Type": "application/json",
                    "X-LUMPI-Token": lan_key,
                    "Content-Length": str(len(raw))
                },
                method="POST"
            )

            try:
                with urlopen(req, timeout=5) as resp:
                    result = json.loads(resp.read().decode())
                    status = "OK" if result.get("result") == expect else f"UNEXPECTED (got {result.get('result')}, expected {expect})"
                    if "UNEXPECTED" in status:
                        unexpected += 1
                    print(f"  Line {line_num}: {status} - raw packet")
            except HTTPError as e:
                err = json.loads(e.read().decode())
                status = "OK" if err.get("error") and expect == "rejected" else f"UNEXPECTED (HTTP {e.code}: {err})"
                if "UNEXPECTED" in status:
                    unexpected += 1
                print(f"  Line {line_num}: {status}")
            except URLError as e:
                print(f"  Line {line_num}: CONNECTION ERROR - {e}")
                unexpected += 1

    print(f"\nDone. Unexpected results: {unexpected}")

if __name__ == '__main__':
    main()