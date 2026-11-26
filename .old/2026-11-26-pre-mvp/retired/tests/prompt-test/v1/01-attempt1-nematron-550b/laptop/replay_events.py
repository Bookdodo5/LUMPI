#!/usr/bin/env python3
"""
Replay a scenario file to the running dashboard server.
Usage: python3 laptop/replay_events.py tests/fixtures/dashboard/normal.jsonl
"""

import os
import sys
import json
import time
import argparse
import requests
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("file", help="Scenario .jsonl file to replay")
    parser.add_argument("--host", default="http://localhost:8201", help="Server base URL")
    parser.add_argument("--key", default=os.environ.get("LUMPI_LAN_KEY", "test"), help="LUMPI_LAN_KEY")
    args = parser.parse_args()

    if not os.path.exists(args.file):
        print(f"File not found: {args.file}", file=sys.stderr)
        sys.exit(1)

    url = f"{args.host}/api/telemetry"
    headers = {"X-LUMPI-Token": args.key, "Content-Type": "application/json"}

    # Generate a new gateway ID for this run
    gateway_id = f"lumpi_{uuid.uuid4().hex[:8]}"
    run_number = int(time.time())
    event_counter = 0

    mismatch = 0
    total = 0

    with open(args.file, "r", encoding="utf-8") as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                entry = json.loads(line)
            except json.JSONDecodeError as e:
                print(f"Line {line_num}: invalid JSON: {e}")
                continue

            delay = entry.get("delay", 0)
            if delay > 0:
                time.sleep(delay)

            expect = entry.get("expect", "added")

            if "event" in entry:
                # Valid event - rewrite event_id to be unique per run
                evt = entry["event"].copy()
                event_counter += 1
                evt["event_id"] = f"{gateway_id}/{run_number}-{event_counter}"
                # Move recorded_at to now
                evt["recorded_at"] = int(time.time())

                resp = requests.post(url, headers=headers, json=evt, timeout=5)
                result = resp.json() if resp.headers.get("content-type", "").startswith("application/json") else {"error": resp.text}
                got = result.get("result") if resp.status_code == 200 else "error"
                if got != expect:
                    print(f"Line {line_num}: expected {expect}, got {got} ({result})")
                    mismatch += 1
                else:
                    print(f"Line {line_num}: {got} (event_id={evt['event_id']})")

            elif "raw" in entry:
                # Bad packet - send raw body
                raw = entry["raw"]
                resp = requests.post(url, headers=headers, data=raw.encode("utf-8"), timeout=5)
                got = "rejected" if resp.status_code >= 400 else "added"
                if got != expect:
                    print(f"Line {line_num}: expected {expect}, got {got} ({resp.text[:100]})")
                    mismatch += 1
                else:
                    print(f"Line {line_num}: {got}")

            total += 1

    print(f"\nDone: {total} messages sent, {mismatch} mismatches")


if __name__ == "__main__":
    main()