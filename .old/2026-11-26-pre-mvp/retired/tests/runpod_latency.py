"""Measure the real AI endpoint on Runpod: cold start, then warm readings.

    python3 tests/runpod_latency.py <endpoint_id>

Uses the API key saved by `rp login` (~/.runpod/config.toml). Costs worker time
(cpu3c-8-16: $0.288/h while a worker runs; about $0.05 for one run of this script).
"""
import base64
import json
import statistics
import subprocess
import sys
import time
import tomllib
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mock_ai import tone  # noqa: E402  (a fake 16 kHz recording; the model's cost does not depend on the words)

KEY = tomllib.loads((Path.home() / ".runpod" / "config.toml").read_text())
KEY = KEY.get("api_key") or next(v["api_key"] for v in KEY.values() if isinstance(v, dict) and "api_key" in v)


# Kill switch: a worker that crashes at start restarts forever and every restart is billed.
# A job still queued after this long means the worker is not coming up.
STUCK_AFTER_S = 180


def stop_workers(endpoint):
    """Set max workers to 0 (no more billing), then print the worker's own error output."""
    subprocess.run(["rp", "endpoint", "logs", endpoint, "--source", "container", "--tail", "40"], timeout=60)
    subprocess.run(["rp", "endpoint", "update", endpoint, "--max-workers", "0"], check=True, timeout=60)


def runsync(endpoint, seconds):
    body = {"input": {"audio_b64": base64.b64encode(tone(seconds, [True, True, True])).decode(), "uid": "FFFFFFFF",
                      "passage": 1, "attempt": 1}}
    request = urllib.request.Request(f"https://api.runpod.ai/v2/{endpoint}/runsync?wait=120000",
                                     data=json.dumps(body).encode(), method="POST",
                                     headers={"Authorization": "Bearer " + KEY, "Content-Type": "application/json"})
    start = time.perf_counter()
    for attempt in range(40):
        try:
            with urllib.request.urlopen(request, timeout=300) as reply:
                answer = json.loads(reply.read())
            break
        except urllib.error.HTTPError as error:
            # 409 right after a deploy: the endpoint is still switching versions. Try again.
            if error.code != 409 or attempt == 39:
                raise
            time.sleep(15)
            start = time.perf_counter()
    # A cold start can outlast runsync's wait; then keep asking until the job ends.
    while answer.get("status") in ["IN_QUEUE", "IN_PROGRESS"]:
        if answer["status"] == "IN_QUEUE" and time.perf_counter() - start > STUCK_AFTER_S:
            stop_workers(endpoint)
            raise RuntimeError(f"job still queued after {STUCK_AFTER_S} s: workers stopped (max 0). See the logs above.")
        time.sleep(1)
        status = urllib.request.Request(f"https://api.runpod.ai/v2/{endpoint}/status/{answer['id']}",
                                        headers={"Authorization": "Bearer " + KEY})
        with urllib.request.urlopen(status, timeout=60) as reply:
            answer = json.loads(reply.read())
    total = (time.perf_counter() - start) * 1000
    if answer.get("status") != "COMPLETED":
        raise RuntimeError(json.dumps(answer)[:500])
    return {"total": total, "queue": answer.get("delayTime", 0), "run": answer.get("executionTime", 0),
            "model": answer["output"]["timing_ms"]["model"], "threads": answer["output"]["timing_ms"]["threads"]}


def main():
    endpoint = sys.argv[1]
    cold = runsync(endpoint, 3)
    print(f"COLD first call: total {cold['total']:.0f} ms (queue+start {cold['queue']} ms, run {cold['run']} ms, "
          f"model {cold['model']} ms, {cold['threads']} threads)")
    for seconds, repeats in [(3, 10), (8, 10)]:
        rows = [runsync(endpoint, seconds) for _ in range(repeats)]
        def stat(key):
            values = [row[key] for row in rows]
            return f"{statistics.mean(values):5.0f} ({min(values):.0f}-{max(values):.0f})"
        print(f"WARM {seconds} s: total {stat('total')}  queue {stat('queue')}  run {stat('run')}  model {stat('model')}  ms")


if __name__ == "__main__":
    main()
