# LUMPI

Build guide: https://lumpi-build-guide.vercel.app

## What we are building

LUMPI is a reading-buddy system for a classroom. Each child has a small doll on
their desk. The doll listens while the child reads, helps them practise a wrong
word and warns them when the room is too noisy. The teacher has one room hub,
called the **nest**, that manages all the dolls.

The main reading flow is:

`Child reads to doll → Nest receives audio → Laptop AI checks words → Nest decides → Doll responds`

The nest also saves the result for the dashboard and can alert the teacher in
Discord when a child is stuck.

### The parts

| Part | What it does |
| --- | --- |
| Doll 1 and Doll 2 | Detect a child and book, select a page, record reading, move, speak and show status |
| Nest | Keep each doll's session separate, measure room light and teacher distance, decide the response and queue cloud events |
| Laptop | Run the pronunciation AI, store book text and word clips, serve the dashboard and send Discord alerts |
| Cloud | Keep reading and room-event history when the Internet is available |

### Reading mode

1. The pressure mat detects a book and RFID identifies it.
2. The child chooses a page and presses Record.
3. The doll records and sends numbered audio pieces to the nest. Missing pieces
   are sent again.
4. The nest sends the complete recording to the laptop AI.
5. The AI scores the words. The nest combines that result with room light and
   the child's recent attempts.
6. The doll responds: praise, practise a word, turn on the light or ask the
   teacher for help.

Both dolls can read at the same time. Every message includes its doll number,
so their audio, results and feedback do not get mixed together.

### Quiet Study mode

The doll reports the room's sound level to the nest. If it stays loud while no
child is reading, the nest tells the doll to ask for quiet. A cooldown prevents
the warning from repeating constantly.

### Safety and reliability

- Every doll has a STOP button that stops the servo pulses, stops sound and
  cancels the current reading. A toggle switch on the servo charger is the
  manual power cut.
- Servo pulses stop after at most 3 seconds.
- Each audio piece is numbered and acknowledged so a lost message can be retried.
- Dolls, nest and laptop work over local Wi-Fi. Reading still works without the
  Internet; cloud events wait until the connection returns.

Detailed explanation: [How LUMPI works](https://lumpi-build-guide.vercel.app/guide/overview)
and [one reading end to end](https://lumpi-build-guide.vercel.app/fundamentals/inner-workings).

For now: testing the parts. Code is in `src/bench`.
Each cpp starts with the wiring and what you should see.

Open this folder in VS Code with PlatformIO installed.
In PlatformIO > Project Tasks, pick the test name, then click Upload.
Then click Monitor. Serial speed is 115200.

Start with `s0_hello` and just the ESP32 plugged into USB.
Then follow the groups below, one part at a time. Numbers give the order
within each group. Run each I2C scan before its OLED or other I2C devices.
The sensor group uses doll wiring; the gateway group uses nest wiring.
The puppet group also uses doll wiring; test its STOP button first.
Opening a cpp does not select it for upload. Pick its environment in Project Tasks.
For the bottom Upload arrow, set `default_envs` in `platformio.ini` to your test name.

For servo commands, use the monitor with Enter-to-send:

```powershell
pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
```

Use ESP32-WROOM-32 boards. Disconnect power before changing wires.
Doll and gateway pin maps differ. Don't wire both roles onto the same board.
No Wi-Fi keys needed for these tests. Never commit `config/private.h` later.

Teacher dashboard: `dashboard/v7`, served by Vercel together with the cloud API
(`api/lumpi.py`, read `docs/cloud-code-guide.md` first). Offline test of the cloud code:

```bash
python3 -m unittest tests.test_cloud tests.test_add_book
```

Dashboard notes: [T05 dashboard work log](docs/t05-dashboard-work-log.md). Screenshots:
`docs/dashboard/screenshot`.

Notes: [test log](docs/test-log.md).

Cloud (Supabase + Vercel + Runpod AI), status and next steps:
[T07 cloud pipeline work log](docs/t07-cloud-pipeline-work-log.md).

Bench tests run in order: the folder, file and environment all start with the
stage number. Step-by-step instructions and what "pass" looks like:
[testing steps](docs/testing-steps.md). Every pin is in `config/settings.h`.

| Stage | Folder `src/bench/` | Environments (PlatformIO > Project Tasks) |
|---|---|---|
| 0 Setup | `0_setup/` | `s0_hello` |
| 1 Doll parts | `1_doll_parts/` | `s1a_buttons`, `s1b_pressure`, `s1c_rfid`, `s1d_microphone`, `s1e_i2c_scan`, `s1f_screen`, `s1g_servos`, `s1h_speaker`, `s1i_mic_to_speaker` |
| 2 Nest parts | `2_nest_parts/` | `s2a_i2c_scan`, `s2b_light`, `s2c_distance`, `s2d_screen`, `s2e_ack_button`, `s2f_bme280_optional` |
| 3 Full doll | `3_full_doll/` | `s3_doll_check` |
| 4 Full nest | `4_full_nest/` | `s4_nest_check` |
| 5 Doll <-> nest (also 7, 8) | `5_doll_nest/` | `s5_nest`, `s5_doll`, `s5_doll_2` |
| 6 Nest -> cloud | `6_nest_cloud/` | `s6_nest_to_dashboard` |
| Extras | `extras/` | `x_i2c_probe` (I2C wire trouble), `x_quiet_study` (feature) |

SH1106 screen instead of SSD1306: use the `_sh1106` version of `s1f`, `s2d`, `s3`, `s4`.

---

## Group project plan

This is the readable version of [LUMPI_Group_Plan.xlsx](LUMPI_Group_Plan.xlsx).
Update status, dates, blockers and evidence in the spreadsheet after each team
check-in.

Spreadsheet snapshot: **29 tasks — 0 completed, 0 in progress, 29 not started.**

### Team

| Member | Role | Assigned tasks |
| --- | --- | ---: |
| 🟥 Khanaphat | Project management and testing | 8 |
| 🟧 Ratchaphon | UI | 3 |
| 🟨 Tanakit | Gateway | 6 |
| 🟩 Thianrawit | Station node | 6 |
| 🟦 Tapanan | Integration | 6 |

### Milestones

| Milestone | Internal target | Official deadline | Evidence |
| --- | --- | --- | --- |
| Checkpoint video | 18 Nov 2026 | **20 Nov 2026, 23:59** | End-to-end video, no longer than 2 minutes |
| Final presentation | 7 Dec 2026 | **8 Dec 2026, 23:59** | Slides, report, video, code and peer evaluation |

### Weekly focus

| Dates | Focus |
| --- | --- |
| 12–15 Nov | Lock interfaces and bench-test station and gateway hardware |
| 16–22 Nov | Complete the checkpoint path and start the main features |
| 23–29 Nov | Finish features, engineering work and two-node integration |
| 30 Nov–8 Dec | Test, fix, document, rehearse and submit |

### Task tracker

All dates are in 2026. The spreadsheet is the editable source for status changes
and blockers.

**Finished:** ✅ Finished · ❌ Not finished

**Owners:** 🟥 Khanaphat · 🟧 Ratchaphon · 🟨 Tanakit · 🟩 Thianrawit · 🟦 Tapanan

| ID | Phase | Task | Owner | Dates | Priority | Status | Finished? | Depends on | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T01 | Checkpoint | Confirm status, assign tasks and lock interfaces | 🟥 Khanaphat | 12–13 Nov | High | Not started | ❌ | — | Shared plan, assigned work, agreed interfaces and recorded blockers |
| T02 | Checkpoint | Station sensors and button bench tests | 🟩 Thianrawit | 12–15 Nov | High | Not started | ❌ | T01 | Repeatable PIR, microphone, pressure, RFID and button results; pin map, wiring photo and logs |
| T03 | Checkpoint | Gateway sensors and node packet reception | 🟨 Tanakit | 12–15 Nov | High | In progress | ❌ | T01 | Software tests pass; real sensor, wiring and packet logs are pending |
| T04 | Checkpoint | Define messages, session state and audio route | 🟦 Tapanan | 12–14 Nov | High | Not started | ❌ | T01 | Versioned schema, example packets and one telemetry/feedback round trip |
| T05 | Checkpoint | Dashboard live sensor and node status | 🟧 Ratchaphon | 13–17 Nov | High | Not started | ❌ | T04 | Two separate node views, live updates, stale-data indication, screenshot and code |
| T06 | Checkpoint | Cloud raw sensor upload and reconnect test | 🟨 Tanakit | 15–17 Nov | High | In progress | ❌ | T03 | Software tests pass; real broker upload and reconnect logs are pending |
| T07 | Checkpoint | Integrate node → gateway → cloud → dashboard | 🟦 Tapanan | 16–18 Nov | **Critical** | Not started | ❌ | T02–T06 | Matching node ID, value and timestamp across the complete physical path |
| T08 | Checkpoint | Record, review and submit checkpoint video | 🟥 Khanaphat | 18–20 Nov | **Critical** | Not started | ❌ | T07 | Real end-to-end video of 2 minutes or less and submission receipt |
| T09 | Core features | Quiet Study: noise, PIR and servo warning | 🟩 Thianrawit | 16–23 Nov | High | Not started | ❌ | T02 | Idle invitation, user start, quiet/noisy tests, cooldown and safe stop |
| T10 | Core features | Book detection, RFID and page selection | 🟩 Thianrawit | 16–23 Nov | High | Not started | ❌ | T02 | Correct book/page, invalid-input handling, removal reset and complete listen events |
| T11 | Core features | Gateway session state and multi-node routing | 🟨 Tanakit | 18–25 Nov | High | Not started | ❌ | T04, T07 | Two independent sessions and histories with no crossed feedback |
| T12 | Core features | Laptop pronunciation AI and reference text/IPA | 🟦 Tapanan | 18–27 Nov | **Critical** | Not started | ❌ | T04, T10 | Correct, incorrect and silent tests return structured results to the right session |
| T13 | Core features | Doll servo and audio feedback | 🟩 Thianrawit | 21–27 Nov | High | Not started | ❌ | T09, T12 | Correct feedback, safe limits, timeout/manual stop and no servo jam |
| T14 | Core features | Teacher presence and low-light prompts | 🟨 Tanakit | 21–26 Nov | High | Not started | ❌ | T03 | Bright/dark and near/far tests, visible state, doll prompt and cooldown |
| T15 | Core features | Teacher dashboard, history and Discord alerts | 🟧 Ratchaphon | 21–28 Nov | High | Not started | ❌ | T06, T11 | Separate live summaries, saved history and test alert without committed webhook |
| T16 | Engineering | E1: FreeRTOS tasks and queues | 🟩 Thianrawit | 20–27 Nov | High | Not started | ❌ | T02 | At least three justified RTOS tasks communicating without missed important events |
| T17 | Engineering | E2: interrupt input and deferred handling | 🟩 Thianrawit | 20–27 Nov | High | Not started | ❌ | T02 | Minimal ISR, deferred work, clean debounce and non-blocking logs |
| T18 | Engineering | E4: ACK/retry and offline buffer | 🟨 Tanakit | 20–28 Nov | High | Not started | ❌ | T03 | Lost-ACK recovery, buffered outage data, no duplicates and delivery measurements |
| T19 | Engineering | E5: TLS, authentication and secret handling | 🟨 Tanakit | 20–28 Nov | High | Not started | ❌ | T06 | Encrypted authenticated traffic, safe invalid login and no secrets in Git |
| T20 | Engineering | E6: OTA update demonstration | 🟦 Tapanan | 23–29 Nov | Medium | Not started | ❌ | T07 | ESP32 updates without USB, reboots and resumes normal operation |
| T21 | Integration | Integrate two physical stations | 🟦 Tapanan | 26–30 Nov | **Critical** | Not started | ❌ | T09–T15 | Both dolls work together with separate sessions and correctly routed feedback |
| T22 | Testing | Run functional and failure tests | 🟥 Khanaphat | 27 Nov–3 Dec | **Critical** | Not started | ❌ | T21 | Pass/Fail for every test; failures have an issue, severity, owner and retest |
| T23 | Testing | Measure latency, delivery rate and AI accuracy | 🟥 Khanaphat | 29 Nov–4 Dec | **Critical** | Not started | ❌ | T21 | At least three metrics with method, sample size, units, raw results and variation |
| T24 | Testing | Run for 24 hours and test recovery | 🟥 Khanaphat | 30 Nov–4 Dec | High | Not started | ❌ | T21 | 24-hour log, uptime calculation and restart/reconnect recovery evidence |
| T25 | Final | Fix defects and validate safety and enclosure | 🟦 Tapanan | 1–5 Dec | **Critical** | Not started | ❌ | T22–T24 | No critical defects, passing regression tests, safe motion and stable release |
| T26 | Final | Prepare slides, report and contribution evidence | 🟥 Khanaphat | 1–6 Dec | **Critical** | Not started | ❌ | T22–T24 | Team-reviewed deliverables, real metrics, required sections and Git evidence |
| T27 | Final | Prepare the public video and backup demo | 🟧 Ratchaphon | 2–6 Dec | High | Not started | ❌ | T21 | Working 3–5 minute public video and playable offline backup |
| T28 | Final | Rehearse the 10-minute demo and Q&A | 🟥 Khanaphat | 6–7 Dec | **Critical** | Not started | ❌ | T25–T27 | Timed rehearsal, tested setup, ready backup and prepared team members |
| T29 | Final | Final demo and submission | 🟥 Khanaphat | 8 Dec | **Critical** | Not started | ❌ | T28 | Demo complete, all files and peer evaluations submitted, receipts checked by 23:59 |

## Project test plan

All test cases are currently **Not started** and owned by Khanaphat. Record the
actual result, Pass/Fail, evidence and issue number in the spreadsheet.

| ID | Due | Scenario | Procedure | Draft target |
| --- | --- | --- | --- | --- |
| Q01 | 18 Nov | End-to-end data flow | Trigger a station sensor and check gateway, cloud and dashboard | All three stages receive the matching node ID |
| Q02 | 2 Dec | Quiet Study noise | Start study, make noise and observe doll motion | Warn above the calibrated threshold; no false warning in quiet |
| Q03 | 2 Dec | RFID and pressure | Place and remove known and unknown books; select a page | Correct book/page; unknown input handled safely |
| Q04 | 4 Dec | Pronunciation accuracy | Compare labeled readings with AI feedback | Report accuracy and precision/recall against labels |
| Q05 | 4 Dec | Latency | Timestamp sensor event and response; repeat 30 times | Report mean, minimum and maximum; agree on a threshold |
| Q06 | 4 Dec | Packet delivery | Send at least 100 numbered packets per node | At least 95% delivery (proposed) |
| Q07 | 4 Dec | 24-hour uptime | Log heartbeats and outages for 24 hours | Report uptime percentage and recovery incidents |
| Q08 | 3 Dec | Internet/cloud failure | Disconnect, generate records and reconnect | Buffered records upload without duplicates |
| Q09 | 3 Dec | Power loss | Power-cycle node and gateway | Automatic recovery and safe idle state |
| Q10 | 3 Dec | Two stations | Read at the same time; compare sessions and feedback | No cross-session mix-ups |
| Q11 | 3 Dec | Light and teacher presence | Vary light and distance; check display and prompts | Correct gateway updates and prompts |
| Q12 | 5 Dec | Safety | Jam or interrupt servo motion; inspect wiring and enclosure | Manual stop or timeout works; no exposed hazards |
