# LUMPI testing steps

Every check, in order, from one bare board to the whole data path.
Do the stages top to bottom: each stage assumes the ones above it passed.

| Stage | What it proves | State (2026-11-22) |
|---|---|---|
| 0. Setup | Board, USB cable, PlatformIO work | Ready |
| 1. Doll parts, one at a time | Each doll part works alone | Ready (microphone failed last run) |
| 2. Nest parts, one at a time | Each nest part works alone | Ready |
| 3. Full doll | All doll parts work wired together | Ready |
| 4. Full nest | All nest parts work wired together | Ready |
| 5. Doll -> nest | The two boards talk | Built: `s5_doll` + `s5_nest`. Not yet run on hardware |
| 6. Nest -> cloud | Nest data reaches the database and dashboard | Built for http (laptop) and https (Vercel). Vercel not deployed yet |
| 7. Doll -> nest -> cloud | A doll's event reaches the dashboard | Built: same sketches, cloud address set. Not yet run on hardware |
| 8. Full data path | A reading goes doll -> nest -> AI -> database -> teacher page | Built. Cloud half tested with stand-ins; board half not yet run |

## How to run any board test

1. Plug the ESP32 into USB.
2. In VS Code: PlatformIO > Project Tasks > pick the **environment** in the table (opening the .cpp does not select it).
3. Click **Upload**, then **Monitor** (115200 baud).
4. For the full checks (stage 3 and 4) use this monitor instead, so you can type answers:

```bash
pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
```

5. Write the result (SUCCESS / NO + what you saw) in `docs/test-log.md`.

Pins: every sketch takes its pins from `config/settings.h` (the wiring sheet). Move a wire = change one number there.
Secrets and addresses: `config/private.h` (copy of `config/private.example.h`, never in git). All boards use the same one.

Where the sketches are:

| Folder | What | Stages |
|---|---|---|
| `src/bench/0_setup/` | First board check | 0 |
| `src/bench/1_doll_parts/` | One doll part each | 1 |
| `src/bench/2_nest_parts/` | One nest part each | 2 |
| `src/bench/3_full_doll/`, `4_full_nest/` | Every part of one board at once | 3, 4 |
| `src/bench/5_doll_nest/` | Doll <-> nest <-> cloud (`s5_nest`, `s5_doll`, `s5_doll_2`) | 5, 7, 8 |
| `src/bench/6_nest_cloud/` | Nest sensors -> dashboard (`s6_nest_to_dashboard`) | 6 |
| `src/bench/extras/` | I2C wire probe, Quiet Study feature | any |
| `tests/fake_doll.py` | The laptop plays a doll, to test `s5_nest` without a doll board | 5, 7, 8 |

---

## Stage 0. Setup

| # | Env | File | Do | Pass when |
|---|---|---|---|---|
| 0.1 | `s0_hello` | `src/bench/0_setup/0_hello.cpp` | Nothing connected, just USB | Monitor prints `ESP32 is running` every second |

---

## Stage 1. Doll parts, one at a time

Wire only the part being tested. Files are in `src/bench/1_doll_parts/`, named `1a_...` to `1h_...`.

| # | Env | Part | Do | Pass when |
|---|---|---|---|---|
| 1.1 | `s1a_buttons` | RECORD (GPIO 13, interrupt), STOP (14), page knob (36) | Press each button; turn the knob | One line per press (holding does not repeat); one line per page step. Turning right = next page |
| 1.2 | `s1b_pressure` | FSR pressure mat (35, 10k to GND) | Press it, put the book on, take it off | Number rises with the book, falls without. Write down both ranges (needed for the book-on threshold) |
| 1.3 | `s1c_rfid` | RC522 RFID (SPI 18/19/23, SS 27). **3.3 V only** | Hold a tag 1-3 cm away | Uppercase UID printed. Save each book's UID. Version `0x00`/`0xFF` = wiring or power fault |
| 1.4 | `s1d_microphone` | INMP441 mic (SCK 26, WS 25, SD 34, L/R GND) | Speak, clap, be quiet | `Peak` rises and falls with sound. Always 0 = check SD and L/R |
| 1.5 | `s1e_i2c_scan` | I2C bus (SDA 32, SCL 33) | Connect the OLED | Scan finds `0x3C` |
| 1.6 | `s1f_screen` or `s1f_screen_sh1106` | Screen | Pick the env for your screen chip | `LUMPI` and a seconds counter on screen. Blank = run 1.5 first |
| 1.7 | `s1g_servos` | Arm (21), tongue (22), STOP (14), servo power from the **separate 5 V charger** | Horns off. Type `a1500`, `t1500`, then `a1400`/`a1600`, then `x` | Servos move and go limp after 3 s or on STOP / `x` |
| 1.8 | `s1h_speaker` | MAX98357A (BCLK 16, LRC 17, DIN 5) | First **Upload Filesystem Image**, then Upload | `data/audio/song.wav` plays on repeat |
| 1.9 | `s1i_mic_to_speaker` | mic + speaker together (as 1.4 and 1.8) | Upload, press RECORD (or type `r`), speak 3 s | you hear your own voice back, clear |

If an I2C part is missing, run `x_i2c_probe` (`src/bench/extras/i2c_wire_probe.cpp`):
unplug and re-plug one wire at a time and watch whether SDA/SCL show short, floating or connected.

---

## Stage 2. Nest parts, one at a time

Files are in `src/bench/2_nest_parts/`, named `2a_...` to `2f_...`.

| # | Env | Part | Do | Pass when |
|---|---|---|---|---|
| 2.1 | `s2a_i2c_scan` | I2C bus (SDA 21, SCL 22) | Connect light sensor and OLED | Scan finds `0x23` (light) and `0x3C` (OLED). `0x5C` = light sensor ADDR is high |
| 2.2 | `s2b_light` | BH1750 light | Cover it, then shine a light | Lux falls, then rises |
| 2.3 | `s2c_distance` | HC-SR04 distance (ECHO through the 1k/2k divider, **never 5 V straight in**) | Hold a flat target at 20, 50, 100 cm | Reading follows the target. `No echo` = timeout, not zero |
| 2.4 | `s2d_screen` or `s2d_screen_sh1106` | Screen | Pick the env for your screen chip | `LUMPI` and a seconds counter |
| 2.5 | `s2e_ack_button` | ACK button | Press, hold, release | One debounced press per click; holding does not add presses |
| 2.6 (optional) | `s2f_bme280_optional` | BME280 spare sensor | Warm it with your hand | Temperature, humidity, pressure; temperature rises |

---

## Stage 3. Full doll (every doll part wired at once)

Proves the parts do not fight each other (shared power, I2C, SPI, I2S).

1. Wire every doll part as in `config/settings.h` (`doll_pins`).
2. Upload env `s3_doll_check` (or `s3_doll_check_sh1106`). File: `src/bench/3_full_doll/3_doll_check.cpp`.
3. Open the interactive monitor (command above).
4. Follow the prompts, about 2 minutes: press each button, turn the knob, put the book down, show a tag, clap, answer y/n for screen, speaker and servos. `s` + Enter skips a step.
5. Pass when the summary at the end shows every part OK.

Feature test on the same wiring: env `x_quiet_study` (`src/bench/extras/quiet_study.cpp`, noise -> servo "shush").
Presence = book on the mat (pressure above `doll_tuning::book_on_fsr` in `settings.h`; the PIR was removed).
Put the book on: after 3 s it listens. Talk loudly for 1 s: the arm waves "shush". Then 30 s rest. Book off for 10 s: idle.

---

## Stage 4. Full nest (every nest part wired at once)

1. Wire light, OLED, distance and ACK as in `config/settings.h` (`gateway_pins`).
2. Upload env `s4_nest_check` (or `s4_nest_check_sh1106`). File: `src/bench/4_full_nest/4_nest_check.cpp`.
3. Open the interactive monitor.
4. Follow the prompts, about 1 minute: cover the light sensor, put a hand in front of the distance sensor, press ACK, answer y/n for the screen.
5. Pass when the summary shows every part OK.

---

## Stage 5. Doll -> nest

Proves the two boards follow the message rules (`src/common/messages.h`, `session.h`): one reading travels as
start -> audio pieces (each ACKed) -> finish -> result. No cloud yet: the nest answers every reading with a stub "1,0 accepted".

### 5a. The rules on the laptop (no boards)

```bash
"/mnt/c/Program Files/LLVM/bin/clang++.exe" -std=c++17 -Wall -Wextra -I src/common src/test/test_round_trip.cpp -o build/test_round_trip.exe
./build/test_round_trip.exe
```

Pass: `ALL PASS: 0 failure(s)`.

### 5b. Nest alone, the laptop plays the doll

1. `config/private.h`: Wi-Fi, `LUMPI_LAN_KEY`, and `LUMPI_ADAPTER_URL ""` (empty = no cloud).
2. Upload env `s5_nest` to the nest (any parts wired, or none). Open Monitor.
3. Pass: it prints `[wifi] connected. Dolls: LUMPI_GATEWAY_URL "http://192.168.x.x"`. Note that address.
4. On the laptop (same Wi-Fi):

```bash
LUMPI_LAN_KEY=<same key> python3 tests/fake_doll.py http://<nest address>
```

5. Pass: every line `PASS` (wrong key refused, presence, start, chunks ACKed, re-send ACKed, finish, result `result,1,0`, old attempt refused, help, noise, status) and `all passed`.
6. Press ACK on the nest, run it again: help now says `coming`.

### 5c. Real doll -> real nest

1. Put the nest's address into `LUMPI_GATEWAY_URL` in `config/private.h` (`"http://192.168.x.x"`).
2. Upload env `s5_doll` to the doll (`s5_doll_2` for a second doll). Open the typing monitor:

```bash
pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
```

3. Doll pass: `[parts] rfid ok, mic ok, mat reads ...` then `[wifi] connected`. Missing parts are allowed: no tag = test UID, no mic = silence, no mat = key `b`.
4. Put the book on (or key `b`). Nest shows `[doll 1] book on`.
5. Show the book's tag. Doll shows `[rfid] book <UID>`.
6. Press RECORD (or key `r`) and read for 3 s. Doll shows the steps; nest shows `start`, `finish`, `result 1,0`.
7. Pass: doll prints `[time] upload ... + grading ... = ... ms` and `[result] 1 = accepted`. Write down the time.
8. Key `h` (help): doll says `waiting`; press ACK on the nest; within 2 s the doll says `coming`.
9. Unplug the doll in the middle of a reading: after 10 s the nest prints `timed out -> idle`.

---

---

## Stage 6. Nest -> cloud

The nest sketch (`src/bench/6_nest_cloud/6_nest_to_dashboard.cpp`, env `s6_nest_to_dashboard`) sends a `room` event every 10 s. Typed commands add mock dolls (`add 1`, `read 1 85`, `help 1` ...; `?` lists them),
so every dashboard card can be tested without a doll board. It speaks http and https. Do the steps in order:

### 6a. Cloud without any board (proves the server and database)

1. Run the unit tests:

```bash
python3 -m unittest tests.test_cloud_api tests.test_dashboard_server tests.test_mock_class
```

2. Run the cloud API against the real Supabase database, with a fake nest, fake AI and fake teacher:

```bash
set -a; source ~/.lumpi-supabase.env; set +a
python3 tests/pipeline_check.py
python3 tests/pipeline_check.py --cleanup
```

3. Pass when unit tests say `OK` and the pipeline check says `15/15 passed`, and cleanup leaves 0 rows.
   (Last run 2026-11-22: 39 tests OK, 15/15.)

### 6b. Real nest -> laptop dashboard (proves Wi-Fi, time and the message format on the board)

1. Copy `config/private.example.h` to `config/private.h`; fill in Wi-Fi name, password, and `LUMPI_LAN_KEY`.
2. On the laptop, with the same key. Use **Windows** Python (PowerShell): under WSL2 without mirrored networking the board cannot reach a server inside WSL. This reads the key straight from `private.h`:

```powershell
$env:LUMPI_LAN_KEY = (Select-String -Path config\private.h -Pattern '#define LUMPI_LAN_KEY "([^"]*)"').Matches[0].Groups[1].Value
python laptop\dashboard_server.py
```

3. Put the address it prints into `LUMPI_ADAPTER_URL` in `config/private.h`.
4. Wire the light and distance sensors. Upload env `s6_nest_to_dashboard`. Open the monitor so you can type:

```bash
pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
```

5. Check the SETTINGS, PARTS and CONNECTION blocks. `Same network? ... NO` means the board cannot reach the laptop.
6. Pass when every line under SENDING says `OK added` and the dashboard nest card updates every 10 s.
7. Type `add 1`, `add 2`, `read 1 85`, `book 2 off`, `help 1`, `noise 2 65`, then `remove 1`: each must change the
   dashboard (doll 1 goes stale after 30 s and offline after 60 s). A failed line prints the raw JSON, the answer, and what it means.

### 6c. Real nest -> cloud API -> Supabase (proves the board's data lands in the real database)

1. On the laptop run the cloud API itself (port 8081), with the Supabase secrets and the same LAN key:

```bash
set -a; source ~/.lumpi-supabase.env; set +a
LUMPI_LAN_KEY=<same key> python3 api/index.py
```

2. Set `LUMPI_ADAPTER_URL` to `http://<laptop IP>:8081`. Upload `s6_nest_to_dashboard` again.
   If the board cannot connect (6b too): the laptop firewall must allow the port, and under WSL2 the
   board cannot reach WSL unless WSL networking is "mirrored" (or run the server with Windows Python).
3. Pass when the monitor shows `200` and new `room` rows from node 0 appear in the Supabase `events` table.

### 6d. Real nest -> Vercel (once the site is deployed)

1. Set `LUMPI_ADAPTER_URL` to `https://<site>.vercel.app`. Upload `s6_nest_to_dashboard`.
2. The monitor warns that the site is not checked until `LUMPI_CLOUD_CA` (the site's root certificate) is set in `config/private.h`. Fine on the bench; set it before a real class.
3. Pass when the monitor shows `200` and the Vercel dashboard's nest card updates.

Not built yet: a flash outbox, so events survive the nest losing power or Wi-Fi for a long time (the link nest keeps the last 16 in memory).

---

## Stage 7. Doll -> nest -> cloud

Same sketches as stage 5; the only change is where the nest sends things.

1. Start a dashboard: either the laptop one (6b, port 8080) or the cloud API on the laptop (6c, port 8081), or Vercel (6d).
2. In `config/private.h` set `LUMPI_ADAPTER_URL` to it. Upload `s5_nest` again (the doll does not change).
3. Book on the doll. Pass: within 10 s the dashboard shows the doll `online`, mode `book_on`, with the book's UID.
4. Wait 30 s. Pass: still `online` (the nest sends a status for each doll every 10 s).
5. Key `h` on the doll. Pass: help popup on the dashboard; the doll shows "Needs help".
6. Key `n` on the doll. Pass: a noise reading on the dashboard (uncalibrated number).
7. Unplug the doll. Pass: `stale` after 30 s, `offline` after 60 s.
8. Turn the Wi-Fi router off for 30 s, then on. Pass: the nest prints `not sent ... retry`, then `200` again; the dashboard fills the gap (up to 16 events kept). Resent events show as `duplicate`, never twice.

No doll board? `tests/fake_doll.py` (5b) does steps 3-6 too: use it with `--node 3`.

---

## Stage 8. Full data path (one reading, end to end)

Proves: doll records -> nest -> cloud API -> AI -> score stored -> teacher page shows it -> doll gets its answer. Goal: under 4 s from end of recording.

### 8a. Cloud half, no boards (already passes)

Stage 6a's pipeline check plays a whole reading with stand-ins. Last run 2026-11-22: 15/15.

### 8b. Boards + cloud API on the laptop + mock AI (free)

1. On the laptop, two terminals:

```bash
python3 tests/mock_ai.py
```

```bash
set -a; source ~/.lumpi-supabase.env; set +a
LUMPI_LAN_KEY=<same key> LUMPI_DASHBOARD_PASSWORD=test LUMPI_AI_URL=http://localhost:8091/v2/mock/runsync LUMPI_AI_KEY=mockkey python3 api/index.py
```

2. `LUMPI_ADAPTER_URL "http://<laptop IP>:8081"`. Upload `s5_nest`. Doll runs `s5_doll` (or `tests/fake_doll.py`).
3. Press RECORD and read (or speak loudly: the mock AI grades loudness).
4. Pass:
   - doll: `[result] 1 = accepted` (loud) or `2 = retry a word` (one quiet third), and `[time] ... < 4000 ms`;
   - nest: `[doll 1] result ... after ... ms`;
   - Supabase `events`: a `score` row for node 1 with the same attempt; `start`/`status` rows around it.
5. Clean up the test rows in Supabase afterwards (node 1, today's attempts).

### 8c. With the real AI (costs money: switch it off after)

1. Turn the Runpod AI on (`docs/t07-cloud-pipeline-work-log.md`, "Switch the AI on and off").
2. Same as 8b but `LUMPI_AI_URL` / `LUMPI_AI_KEY` = the Runpod endpoint (or use Vercel, 6d, which has them set).
3. Read the passage properly. Pass: a real score with per-word and per-sound results on the dashboard, doll result under 4 s.
4. Read again: `attempt` goes up by 1; the dashboard shows the improvement.
5. Turn the AI off again.

