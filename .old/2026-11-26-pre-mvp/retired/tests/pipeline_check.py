"""Whole cloud pipeline with stand-ins: fake nest -> cloud API -> mock AI -> Supabase -> mock teacher.

Real: api/index.py and the Supabase database. Stand-ins: the nest (this script),
the AI (tests/mock_ai.py) and the teacher's browser (this script reads the pages' routes).
Not a unit test (it uses the real database), so it is not named test_*.py.

    set -a; source ~/.lumpi-supabase.env; set +a
    python3 tests/pipeline_check.py             # run, leave rows for inspection
    python3 tests/pipeline_check.py --cleanup   # delete this script's rows (uid FFFFFFFF, ids pipetest/...)
"""
import base64
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "api"))
sys.path.insert(0, str(ROOT / "tests"))

TEST_UID = "FFFFFFFF"
LAN_KEY = "pipetest-key"
PASSWORD = "pipetest-pw"
API = "http://localhost:8092"


def database(method, path):
    key = os.environ["SUPABASE_SECRET_KEY"]
    request = urllib.request.Request(os.environ["SUPABASE_URL"] + "/rest/v1/" + path, method=method, headers={"apikey": key})
    with urllib.request.urlopen(request) as reply:
        raw = reply.read()
    return json.loads(raw) if raw else None


def cleanup():
    database("DELETE", "events?event_id=like.pipetest*")
    database("DELETE", f"events?event_type=eq.score&data->>uid=eq.{TEST_UID}")
    database("DELETE", "rejects?source=in.(127.0.0.1,ai)")
    left = database("GET", f"events?select=event_id&or=(event_id.like.pipetest*,data->>uid.eq.{TEST_UID})")
    print(f"cleanup done, test rows left: {len(left)}")


def call(method, path, body=b"", headers=None):
    request = urllib.request.Request(API + path, data=body if method == "POST" else None, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(request, timeout=30) as reply:
            return reply.status, reply.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8")


def main():
    if "--cleanup" in sys.argv:
        cleanup()
        return

    os.environ.update({"LUMPI_LAN_KEY": LAN_KEY, "LUMPI_DASHBOARD_PASSWORD": PASSWORD,
                       "LUMPI_AI_URL": "http://localhost:8091/v2/mock/runsync", "LUMPI_AI_KEY": "mockkey",
                       "LUMPI_BOOKS": str(ROOT / "tests" / "fixtures" / "books.json")})  # the mock AI's test book
    import index as cloud
    import mock_ai

    for port, handler in [(8091, mock_ai.MockAi), (8092, cloud.handler)]:
        server = ThreadingHTTPServer(("127.0.0.1", port), handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()

    results = []

    def check(stage, ok, detail=""):
        results.append((stage, ok, detail))
        print(f"{'PASS' if ok else 'FAIL'}  {stage}  {detail}")

    run = int(time.time())
    number = [0]

    def nest_send(node, kind, at, activity=None, **data):
        number[0] += 1
        event = {"event_id": f"pipetest/{run}-{number[0]}", "node_id": node, "event_type": kind,
                 "recorded_at": at, "activity": activity, "data": data}
        code, text = call("POST", "/api/telemetry", json.dumps(event).encode(), {"X-LUMPI-Token": LAN_KEY})
        return event, code, text

    def nest_assess(attempt, audio, key=LAN_KEY):
        return call("POST", f"/v1/assess?node=1&attempt={attempt}&passage=1&uid={TEST_UID}", audio,
                    {"Authorization": "Bearer " + key, "Content-Type": "application/octet-stream"})

    now = int(time.time())
    book = {"uid": TEST_UID, "passage": 1, "attempt": 1, "fsr": 3000}
    no_book = {"uid": None, "passage": None, "attempt": None, "fsr": 5}

    # 1. Nest sensors and doll states go up.
    room, code, text = nest_send(0, "room", now - 50, lux=187.5, distance_cm=None)
    check("nest room event stored", code == 200 and "added" in text, text)
    nest_send(1, "status", now - 45, "idle", **no_book)
    book_on, _, _ = nest_send(1, "start", now - 40, "book_on", **book)
    nest_send(1, "status", now - 35, "recording", **book)
    nest_send(2, "status", now - 30, "quiet_study", **no_book)

    # 2. Readings go to the AI (it only transcribes) and back, graded by the cloud API.
    code, text = nest_assess(1, mock_ai.tone(3, [True, True, False]))
    check("assess: last word read badly -> 2,2 + its correct sounds and clip",
          (code, text) == (200, f"2,2\n2,swims,s w ɪ m z,/v1/word_audio?uid={TEST_UID}&passage=1&word=2"), f"{code} {text!r}")
    nest_send(1, "status", now - 20, "feedback", **book)
    code, text = nest_assess(2, mock_ai.tone(3, [True, True, True]))
    check("assess: all words good -> 1,0", (code, text) == (200, "1,0"), f"{code} {text}")

    # 3. Failures.
    os.environ["LUMPI_AI_URL"] = "http://localhost:8099/nothing-here"
    code, text = nest_assess(3, mock_ai.tone(3, [True, True, True]))
    check("AI down -> 4,0 (uncertain), no crash", (code, text) == (200, "4,0"), f"{code} {text}")
    os.environ["LUMPI_AI_URL"] = "http://localhost:8091/v2/mock/runsync"
    check("assess with wrong key -> 401", nest_assess(4, b"\0\0", key="wrong")[0] == 401)
    check("recording over 8 s -> 400", nest_assess(5, b"\0" * 256002)[0] == 400)
    code, text = call("POST", "/api/telemetry", json.dumps(room).encode(), {"X-LUMPI-Token": LAN_KEY})
    check("same event sent twice -> duplicate", "duplicate" in text, text)

    # 4. The teacher's pages.
    auth = {"Authorization": "Basic " + base64.b64encode(f"teacher:{PASSWORD}".encode()).decode()}
    check("dashboard without password -> 401", call("GET", "/api/dashboard/nodes")[0] == 401)
    nodes = {n["node_id"]: n for n in json.loads(call("GET", "/api/dashboard/nodes", headers=auth)[1])}
    check("doll 1 shows feedback with its book", nodes[1]["activity"] == "feedback" and nodes[1]["values"]["uid"] == TEST_UID)
    check("doll 2 shows quiet_study", nodes[2]["activity"] == "quiet_study")
    scores = [s for s in json.loads(call("GET", "/api/dashboard/scores?day=today", headers=auth)[1])["scores"]
              if s["uid"] == TEST_UID and s["recorded_at"] >= now]  # this run only, not rows left by an earlier run
    check("2 AI scores stored (failed AI stored none)", len(scores) == 2, f"{[s['score'] for s in scores]}")
    check("score session = book_on time", all(s["session"] == book_on["recorded_at"] for s in scores),
          f"{[s['session'] - now for s in scores]} vs {book_on['recorded_at'] - now}")

    # 5. T07 step 3: the same node, value and time at every stage.
    row = database("GET", f"events?select=*&event_id=eq.{room['event_id']}")[0]
    history = json.loads(call("GET", "/api/dashboard/history?minutes=5", headers=auth)[1])["nodes"]["0"]
    point = [p for p in history if p["recorded_at"] == room["recorded_at"]]
    same = (row["node_id"], row["recorded_at"], row["data"]["lux"]) == (room["node_id"], room["recorded_at"], room["data"]["lux"])
    check("T07: nest event == database row", same, f"node {row['node_id']} t {row['recorded_at']} lux {row['data']['lux']}")
    check("T07: database row == dashboard point", bool(point) and point[0]["data"]["lux"] == room["data"]["lux"])
    check("delivery latency (received - recorded) shown", True, f"{row['received_at'] - row['recorded_at']:.1f} s (old timestamps on purpose)")

    failed = [stage for stage, ok, _ in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed. Rows left in Supabase; remove with --cleanup")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
