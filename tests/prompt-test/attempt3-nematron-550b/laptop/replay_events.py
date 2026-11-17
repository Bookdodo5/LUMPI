#!/usr/bin/env python3
"""
Replay a scenario JSONL file to the running dashboard server.
Each line: {"delay": 1.5, "event": {...}, "expect": "added"}
or:        {"delay": 0.2, "raw": "broken text", "expect": "rejected"}

Moves recorded_at to now and makes event IDs unique per run
(<gateway>/<run>-<number>) so the same file can be replayed without a restart
while duplicates inside a file stay duplicates.
"""

import sys
import json
import time
import os
import hmac
import uuid
import argparse
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError

def send_event(server_url, token, event, recorded_at_offset=0):
    """Send an event to the server, return (status, response_text)."""
    # Add recorded_at if not present (will be overwritten by server's check anyway)
    if "recorded_at" not in event:
        event["recorded_at"] = int(time.time()) + recorded_at_offset

    body = json.dumps(event).encode("utf-8")
    req = Request(
        f"{server_url}/api/telemetry",
        data=body,
        headers={
            "Content-Type": "application/json",
            "X-LUMPI-Token": token,
            "Content-Length": str(len(body))
        },
        method="POST"
    )
    try:
        with urlopen(req, timeout=5) as resp:
            return resp.status, resp.read().decode("utf-8")
    except HTTPError as e:
        return e.code, e.read().decode("utf-8")
    except URLError as e:
        return 0, str(e)

def send_raw(server_url, token, raw_text):
    """Send raw (invalid) body to the server."""
    body = raw_text.encode("utf-8")
    req = Request(
        f"{server_url}/api/telemetry",
        data=body,
        headers={
            "Content-Type": "application/json",
            "X-LUMPI-Token": token,
            "Content-Length": str(len(body))
        },
        method="POST"
    )
    try:
        with urlopen(req, timeout=5) as resp:
            return resp.status, resp.read().decode("utf-8")
    except HTTPError as e:
        return e.code, e.read().decode("utf-8")
    except URLError as e:
        return 0, str(e)

def main():
    parser = argparse.ArgumentParser(description="Replay scenario to LUMPI dashboard server")
    parser.add_argument("file", help="Scenario JSONL file")
    parser.add_argument("--url", default="http://localhost:8203", help="Server URL")
    parser.add_argument("--token", default=os.environ.get("LUMPI_LAN_KEY", "test"), help="LUMPI_LAN_KEY token")
    args = parser.parse_args()

    if not os.path.exists(args.file):
        print(f"File not found: {args.file}", file=sys.stderr)
        sys.exit(1)

    # Generate a run ID for unique event_ids
    run_id = str(uuid.uuid4())[:8]

    with open(args.file, "r") as f:
        lines = f.readlines()

    print(f"Replaying {len(lines)} lines from {args.file} to {args.url}")
    print(f"Run ID: {run_id}")
    print(f"Token: {args.token}")

    mismatches = 0
    event_counter = 0

    for i, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        try:
            item = json.loads(line)
        except json.JSONDecodeError as e:
            print(f"Line {i}: invalid JSON: {e}", file=sys.stderr)
            continue

        delay = item.get("delay", 0)
        if delay > 0:
            time.sleep(delay)

        expect = item.get("expect", "added")

        if "event" in item:
            event = dict(item["event"])  # copy
            event_counter += 1

            # Rewrite event_id to be unique per run: <gateway>/<run>-<number>
            # Extract gateway part before the slash
            old_id = event.get("event_id", "")
            if "/" in old_id:
                gateway = old_id.split("/")[0]
            else:
                gateway = "lumpi_replay"
            event["event_id"] = f"{gateway}/{run_id}-{event_counter}"

            # recorded_at will be set to now by send_event
            status, resp_text = send_event(args.url, args.token, event)

            try:
                resp = json.loads(resp_text)
                result = resp.get("result", resp.get("error", "unknown"))
            except json.JSONDecodeError:
                result = f"invalid response: {resp_text[:60]}"

            ok = (expect == "added" and result == "added") or \
                 (expect == "rejected" and result != "added") or \
                 (expect == "duplicate" and result == "duplicate")

            status_str = "OK" if ok else "MISMATCH"
            if not ok:
                mismatches += 1
            print(f"  Line {i:3d}: {status_str} | sent event_id={event['event_id']} | got result={result} | expected={expect}")

        elif "raw" in item:
            raw = item["raw"]
            status, resp_text = send_raw(args.url, args.token, raw)

            try:
                resp = json.loads(resp_text)
                result = resp.get("error", resp.get("result", "unknown"))
            except json.JSONDecodeError:
                result = f"invalid response: {resp_text[:60]}"

            ok = (expect == "rejected" and result != "added")
            status_str = "OK" if ok else "MISMATCH"
            if not ok:
                mismatches += 1
            print(f"  Line {i:3d}: {status_str} | sent raw | got result={result} | expected={expect}")

        else:
            print(f"  Line {i:3d}: SKIP (no event or raw)", file=sys.stderr)

    print(f"\nDone. Mismatches: {mismatches}")
    if mismatches > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()