# T05 Dashboard work log

Owner: Ratchaphon
Task: Dashboard live sensor/node status

## Current status

Works with mock data. Not yet connected to the real nest (T07).

## How to run

```bash
LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py
# open http://localhost:8080, then in another terminal:
LUMPI_LAN_KEY=test python3 laptop/replay_events.py tests/fixtures/dashboard/doll2_dies.jsonl
```

Record a real lesson, and play it again later:

```bash
LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py --record build/lesson.jsonl
LUMPI_LAN_KEY=test python3 laptop/replay_events.py build/lesson.jsonl
```

Send one test event by hand:

```bash
curl -X POST http://localhost:8080/api/telemetry -H "X-LUMPI-Token: test" \
  -d '{"event_id":"lumpi_test/1","node_id":0,"event_type":"room","recorded_at":1794794400,"activity":null,"data":{"lux":42.0,"distance_cm":null}}'
```

Nest firmware: fill `LUMPI_ADAPTER_URL` (the server prints it at startup) and
`LUMPI_LAN_KEY` in `config/private.h`, then upload environment `dashboard_post`.
If the ESP32 prints "cannot reach", check that the laptop firewall allows
port 8080.

Tests:

```bash
python3 -m unittest tests.test_dashboard_server
g++ -std=c++17 -I src/common tests/test_dashboard_event.cpp -o build/test_dashboard_event && ./build/test_dashboard_event
```

## Work completed

2026-11-16

- Added `laptop/dashboard_server.py`, Python standard library only.
- Added the page in `dashboard/`: nest card, two doll cards, recent events.
- Added 6 mock scenarios in `tests/fixtures/dashboard/`: normal, doll2_dies,
  flaky_wifi, sensor_fail, busy, bad_packets.
- Added `laptop/replay_events.py` to play a scenario to the running server.
- Tests play every scenario through the server with a fake clock.
- Broke three server rules on purpose; tests failed each time. One scenario
  line was rejected for the wrong reason, so it was fixed.
- Live replay found a bug: an idle doll kept showing the old book. Doll
  heartbeats now carry the full doll state (PRD section 3).
- Live check found the badges still said ONLINE when the server was down.
  They now say NO SERVER.

- Added trend charts (plain SVG in `dashboard/charts.js`) with a 5 min /
  15 min / 1 h toggle: light, distance, message timeline with gaps, activity
  and pressure per doll. Hover shows time and value.
- Added message counters per node (received, duplicate) for T23 delivery rate.
- Added a delay column and click-open JSON in recent events.
- Added the developer panel: last 50 rejected messages, each with reason,
  fix hint, raw packet and a correct example. Wrong-token bodies are not kept.
- Added a "Room is dark" note on the nest card under 100 lux.
- Fixed: charts drawn at real pixel width so text is the same size everywhere.
- Fixed: duplicates in `flaky_wifi` pushed later events late; replay lag is
  now under 1 s.

- Added `src/common/dashboard_event.h`: builds room and doll start events in
  C++ for the nest. Failed sensors become null. RFID UIDs must be hex, so a
  bad UID cannot break the JSON. Event IDs include the boot time, so a
  restarted nest is not dropped as duplicates.
- Added `src/bench/gateway_6_dashboard_post.cpp` (environment `dashboard_post`):
  sends one sample start packet through the real parser, then light and
  distance every 10 s, and prints the server reply.
- A Python test compiles the C++ builder and checks its output with the server.
- Server uses `LUMPI_LAN_KEY` (the key already in `private.example.h`) instead
  of a separate dashboard token, prints the LAN address at startup, and writes
  its log line by line.
- Added `--record`: every checked message is saved in the scenario format and
  can be replayed.

## Evidence

| Check | Result |
| --- | --- |
| Automated tests (21 dashboard + 3 cloud worker) | Passed |
| Trend charts, toggle and hover | Passed by hand, screenshot 5 |
| Every rejected message has a fix hint | Passed (automated) |
| Open error and event details stay open while the page refreshes | Passed by hand, screenshot 6 |
| All cards UNKNOWN at start | Passed |
| Doll 1 and Doll 2 stay separate | Passed |
| Doll 2 stale after 30 s, offline after 60 s | Passed, screenshots 2 and 3 |
| Bad packets rejected, state unchanged | Passed, 20 bad lines |
| Duplicate event stored once | Passed |
| Late older event does not overwrite newer values | Passed |
| Server stop and restart, page recovers | Passed by hand, screenshot 4 |
| C++ event builder (native test) | Passed |
| C++ output accepted by the server | Passed (automated) |
| `gateway_6_dashboard_post.cpp` | Checked against stand-in Arduino headers only; not built with PlatformIO yet |
| Record, then replay into a fresh server | Passed (automated and live) |
| Real nest event | Pending (T07) |

## Next work

1. Get Tapanan to approve the event contract (PRD section 3).
2. Agree the nest-to-laptop transport, then send real events in T07.
