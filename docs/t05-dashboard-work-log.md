# T05 Dashboard work log

Owner: Ratchaphon
Task: Dashboard live sensor/node status

## Current status

Works with mock data. Not yet connected to the real nest (T07).
Default page is `dashboard/v7`; the developer page is at `/debug/`.

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
`LUMPI_LAN_KEY` in `config/private.h`, then upload environment `s6_nest_to_dashboard`.
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
- Added `src/bench/6_nest_cloud/6_nest_to_dashboard.cpp` (environment `s6_nest_to_dashboard`):
  sends one sample start packet through the real parser, then light and
  distance every 10 s, and prints the server reply.
- A Python test compiles the C++ builder and checks its output with the server.
- Server uses `LUMPI_LAN_KEY` (the key already in `private.example.h`) instead
  of a separate dashboard token, prints the LAN address at startup, and writes
  its log line by line.
- Added `--record`: every checked message is saved in the scenario format and
  can be replayed.

2026-11-18

- v6: teacher pages built from the approved wireframes in
  `docs/dashboard/ref` (Overview, Doll detail, Reading session). Light gauge
  is live and shows the lux under the lamp. Noise shows dB, but it is sample
  data: the doll microphone is not sent or calibrated yet.
- v7 (now the default): doll rows, nest status and recent activity are live.
  Offline dolls get a dashed "Last seen" chip and go to the top. Activity lists
  only changes from the last hour, not every heartbeat.
- The v5 page is served at `/debug/` ("Advanced / debug" link) for rejected
  messages, counters and delay. `LUMPI_DASHBOARD=v1` to `v6` still serves an
  older page at `/`.
- Still sample data: scores, words, sessions (need T12), noise dB (T09), cloud
  queue (T06).
- Screenshots: `docs/dashboard/screenshot/dashboard-v6-screenshots` and
  `dashboard-v7-screenshots`.
- Added mock scenario `tests/fixtures/dashboard/teacher_view.jsonl`: every
  doll activity, every light zone, failed sensors, teacher far, a duplicate
  and a late message. Played by `test_teacher_view`.
- v7 live test, one screenshot per case (01-21). Fixed what it found:
  - Before the first nest message the gauge showed the sample "420 lx, teacher
    near: Yes" as if live. Now shows "Waiting" and "—".
  - Stale chip said "Quiet 40 s", easy to mix up with Quiet Study. Now
    "No msg 40 s"; the header says "No message from the nest for 40 s".
  - Offline nest still said "Teacher near nest: Yes". Now adds "(old)".
  - Header wrapped onto two lines at 1280 px. Now one line.
  - "Cloud offline · 12 queued" was wireframe text, not real. The nest does
    not report its cloud queue yet, so it now says "Cloud: no status yet".
- v7 shows no sample data any more. Everything is real, or 0 / "no data yet":
  - Live: dolls, Needs help, Mode (last hour, from activity history), recent
    activity, light, teacher near, nest. Doll page (`doll.html?doll=1`):
    mode, state, book, page, mode changes, minutes per mode.
  - Needs help = 3rd try or more on a page, or STOP pressed. Placeholder until
    the nest sends its own "ask the teacher" event (T11/T12).
  - 0 / empty until data exists: scores, Improved, words, sessions (T12),
    noise dB (T09), days read this week (T15). Session page says "No reading
    sessions yet". Today / This week toggle removed (no saved history yet).
  - Shared page code moved to `dashboard/v7/shared.js`.
  - Server stopped answering everyone when Chrome left a spare connection
    open with nothing sent. Now uses ThreadingHTTPServer with one lock;
    `test_idle_connection_does_not_block` fails on the old server.

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
| v7: empty, all online, reading, Quiet Study, STOP | Passed by hand, v7 screenshots 01-04 |
| v7: light too dark / a bit dark / a bit bright / too bright, sensor failed, teacher far | Passed by hand, v7 screenshots 05-10 |
| v7: late message keeps newer state | Passed by hand (11) and automated |
| v7: doll stale, doll offline, nest stale, nest offline, server lost and back | Passed by hand, v7 screenshots 12-16 |
| v7: debug page at /debug/ with rejected messages | Passed by hand, v7 screenshot 19 |
| v7: 1280x800 laptop screen, no sideways scroll | Passed by hand, v7 screenshots 20-21 |
| Idle browser connection does not block the server | Passed (automated) |
| Real nest event | Pending (T07) |

## Next work

1. Get Tapanan to approve the event contract (PRD section 3).
2. Agree the nest-to-laptop transport, then send real events in T07.

2026-11-18 (v7 scores)

- Laptop AI format fixed: POST /api/telemetry, same X-LUMPI-Token, one
  `score` event per reading attempt. Example in `EXAMPLES["score"]` in
  `laptop/dashboard_server.py`: overall score, then every word with its score
  and its IPA sounds `{ipa, score}`. All scores 0-100. Body limit now 8192.
- New nest events: `help` (child pressed help, data `{}`) and `noise`
  (data `{db}`, sent only in Quiet Study). Nest firmware still has to send them.
- Session = from the child picking Reading until the doll leaves Reading. The
  server tags each score with its session and saves it to
  `data/scores/<day>.jsonl` (gitignored). GET /api/dashboard/scores?day=today|past.
- Overview: Today / Past classes toggle. Score trend on the class clock
  (today) or per class day (past). Reading score card replaced by
  Pronunciation by sound (IPA): average per sound with its weakest words.
  Words card shows worst words with avg accuracy. Needs help KPI removed,
  replaced by a popup when a child presses help. Room: noise replaced by
  teacher distance.
- Doll page: same toggle, latest session, words to practise, own IPA sounds,
  session list + chart, noise only in Quiet Study.
- Needs help is now only the help event (5 min). The 3rd-try rule is gone.

2026-11-18 (v7 round 2)

- `laptop/mock_class.py`: a fake class (past days + today) sent through
  POST /api/telemetry with the token, like real hardware. Every event type,
  10 s heartbeats, sessions, scores with words and IPA, noise in Quiet Study,
  help. `--seed` changes every number. `tests/test_mock_class.py` plays it
  through the server's checks and compares what is saved with what was sent.
- Mode bar gaps seen earlier came from sparse test data, not the page. With
  10 s heartbeats the bar is solid.
- Server saves one room reading a minute to `data/room/<day>.jsonl`.
  History keeps 3 hours (`minutes=180`) for the doll's all-day timeline.
  `help_at` is now the nest's time, so a late press keeps its real time.
- Overview: Improved says "today" / "overall". Past classes shows light vs
  score and teacher distance vs score (1 dot per session, with r). Header
  circle removed. "Passages →" opens the class passage page.
- Doll page: score meter + Read the passage at top left, one timeline (mode,
  noise in Quiet Study, scores, help), summary at top right, Did practising
  help (1st try → latest per word).
- Session page (`session.js`): class view = each passage, word = class
  average. `?doll=N` = that doll's latest session, every try word by word.
- Noise bands 60 / 70 dB are placeholders until the mic is calibrated (T09).

2026-11-18 (v7 round 3)

- Word lists and passage cells show why a word is wrong: its weakest IPA
  sounds, e.g. "through · Weak sound: /θ/ 43% · /r/ 55%".
- Did practising help is now plain rows: first % → latest %, change chip,
  weak sound, reads.
- Overview Past: Mode and Recent activity (live) are replaced by "Class by
  class" (date, sessions, tries, average, change, weakest sound).
- Session page: "‹ Doll N" / "‹ Overview" back button under the header,
  like the doll page.
- Earlier "one doll in Score trend": in that mock run Doll 2 never read
  today. The mock now starts every doll with a reading block.
- `docs/dashboard-api.md`: message format and routes for the nest and the
  laptop AI. `test_api_doc_examples_are_accepted` checks every JSON example
  in it against the server.

- Session page: every word shows all its IPA sounds in reading order, each
  with its own %, so the teacher sees which sound made the word wrong. The
  weakest sound is outlined. Class view averages each sound over every read.
- Session page: "Lowest pronunciation" card (IPA sounds, weakest first,
  with the words they are in), next to "Lowest words" under the passage.
- Session page: IPA sounds under each word are now only right (green ✓)
  or wrong (red ✗, under 60). No percentage.
- Fixed mock data: `tests/fixtures/dashboard/mock_class.jsonl` (seed 7,
  4 past days + today, 2314 messages). `mock_class.py --load` sends it with
  times moved so today ends now; same numbers every run. `--save` makes a new one.
