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

If an I2C part is missing, check that module's SDA, SCL, power and ground wires one at a time, then scan again.

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

### 5b. Nest alone

Retired 2026-11-26: the laptop fake doll (`tests/fake_doll.py`) is kept in
`.old/2026-11-26-pre-mvp/retired/tests/`. Use a real doll board (5c).

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
python3 -m unittest tests.test_cloud tests.test_add_book
```

2. Pass when it says `OK` (20 tests: 17 cloud, 3 book tool; Supabase, Storage and the AI are faked in memory).

### 6b. Real nest -> cloud API run on the laptop (proves Wi-Fi, time and the message format on the board)

1. Copy `config/private.example.h` to `config/private.h`; fill in Wi-Fi name, password, and `LUMPI_LAN_KEY`.
2. On the laptop, with the same key. Use **Windows** Python (PowerShell): under WSL2 without mirrored networking the board cannot reach a server inside WSL. This reads the key straight from `private.h`:

```powershell
$env:LUMPI_LAN_KEY = (Select-String -Path config\private.h -Pattern '#define LUMPI_LAN_KEY "([^"]*)"').Matches[0].Groups[1].Value
# also set SUPABASE_URL and SUPABASE_SECRET_KEY (from ~/.lumpi-supabase.env) the same way
python api\lumpi.py
```

3. Put `http://<laptop IP>:8081` into `LUMPI_ADAPTER_URL` in `config/private.h` (the firewall must allow port 8081).
4. Wire the light and distance sensors. Upload env `s6_nest_to_dashboard`. Open the monitor so you can type:

```bash
pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
```

5. Check the SETTINGS, PARTS and CONNECTION blocks. `Same network? ... NO` means the board cannot reach the laptop.
6. Pass when every line under SENDING says `OK added` and new `room` rows from node 0 appear in the Supabase `events` table.
7. With the dashboard on Vercel (6c) open: type `add 1`, `add 2`, `read 1 85`, `book 2 off`, `help 1`, `noise 2 65`, then `remove 1`: each must change the
   dashboard (doll 1 goes stale after 30 s and offline after 60 s). A failed line prints the raw JSON, the answer, and what it means.

### 6c. Real nest -> Vercel (once the site is deployed)

1. Set `LUMPI_ADAPTER_URL` to `https://<site>.vercel.app`. Upload `s6_nest_to_dashboard`.
2. The monitor warns that the site is not checked until `LUMPI_CLOUD_CA` (the site's root certificate) is set in `config/private.h`. Fine on the bench; set it before a real class.
3. Pass when the monitor shows `200` and the Vercel dashboard's nest card updates.

Not built yet: a flash outbox, so events survive the nest losing power or Wi-Fi for a long time (the link nest keeps the last 16 in memory).

---

## Stage 7. Doll -> nest -> cloud

Same sketches as stage 5; the only change is where the nest sends things.

1. Start the cloud: either the cloud API on the laptop (6b, port 8081) or Vercel (6c).
2. In `config/private.h` set `LUMPI_ADAPTER_URL` to it. Upload `s5_nest` again (the doll does not change).
3. Book on the doll. Pass: within 10 s the dashboard shows the doll `online`, mode `book_on`, with the book's UID.
4. Wait 30 s. Pass: still `online` (the nest sends a status for each doll every 10 s).
5. Key `h` on the doll. Pass: help popup on the dashboard; the doll shows "Needs help".
6. Key `n` on the doll. Pass: a noise reading on the dashboard (uncalibrated number).
7. Unplug the doll. Pass: `stale` after 30 s, `offline` after 60 s.
8. Turn the Wi-Fi router off for 30 s, then on. Pass: the nest prints `not sent ... retry`, then `200` again; the dashboard fills the gap (up to 16 events kept). Resent events show as `duplicate`, never twice.


---

## Stage 8. Full data path (one reading, end to end)

Proves: doll records -> nest -> cloud API -> AI writes down the sounds -> cloud API grades them against the book's sounds -> score stored -> teacher page shows it -> doll gets its answer. Goal: under 4 s from end of recording.

### 8a. Cloud half, no boards (already passes)

Stage 6a's unit tests play a whole reading with stand-ins (fake database, fake AI).

### 8b. Boards + cloud (real AI)

The free mock AI and the books-from-a-file switch were retired on 2026-11-26 (kept in
`.old/2026-11-26-pre-mvp/`). Books now always come from Supabase Storage: add yours with
`tools/add_book.py` (your tag's UID), then do 8c.

### 8c. With the real AI (costs money: switch it off after)

1. Turn the Runpod AI on (`docs/t07-cloud-pipeline-work-log.md`, "Switch the AI on and off").
2. Run the cloud API on the laptop with `LUMPI_AI_URL` / `LUMPI_AI_KEY` = the Runpod endpoint (as in 6b, plus
   `LUMPI_DASHBOARD_PASSWORD`), or use Vercel (6c), which has them set. `LUMPI_ADAPTER_URL` = that address;
   upload `s5_nest`; the doll runs `s5_doll`. Use a tag whose book was added with `tools/add_book.py`.
3. Read the passage properly. Pass: a real score with per-word and per-sound results on the dashboard, doll result under 4 s.
   The cloud's answer: line 1 `code,word`; for code 2 also one line per wrong word,
   `<index>,<word>,<correct sounds>,/v1/word_audio?...`. Supabase `events` gets a `score` row for that node.
   Clean up the test rows in Supabase afterwards.
4. Read again: `attempt` goes up by 1; the dashboard shows the improvement.
5. Turn the AI off again.

---

## Stage 9. Real doll firmware (`src/app/station/doll.cpp`)

Wiring: full doll wiring from `config/settings.h` (`doll_pins`). Every part is optional at start-up (missing mic sends silence; missing RFID uses `04A1B2C3`; the speaker needs a working MAX98357A amp).
Upload env: `doll` (doll 1), `doll_2` (doll 2), or `doll_sh1106` (SH1106 screen). Monitor: `115200`.

### 9a. Laptop logic tests (no boards)

```powershell
g++ -std=c++17 -Wall -Wextra -Werror -I src/common src/test/test_round_trip.cpp -o build/test_round_trip.exe
.\build\test_round_trip.exe
g++ -std=c++17 -Wall -Wextra -Werror -I src/common tests/test_dashboard_event.cpp -o build/test_dashboard_event.exe
.\build\test_dashboard_event.exe
g++ -std=c++17 -Wall -Wextra -Werror -I src/common tests/test_doll_logic.cpp -o build/test_doll_logic.exe
.\build\test_doll_logic.exe
```

Pass: every binary prints `PASS` / `0 failure(s)`.

### 9b. Doll hardware checks (with real nest running the updated contract)

| # | Feature | Do | Pass when |
|---|---|---|---|
| 9.1 | Boot + CSV (D1, D11) | Power doll 1; watch Monitor | Servos stay limp at boot; prints `CSV,boot,1,...` with free heap and `Audio pool ... 10 x 8192 B` (or fallback); `CSV,beat,1,...` prints every 60 s |
| 9.2 | RFID book tag (D3) | Present every book tag | OLED shows new book UID/title and resets page to 1 (matches `s1c_rfid`) |
| 9.3 | Buttons by interrupt (D2, E2) | Press RECORD and STOP 50 times cleanly | 50 debounced events, no double-triggers |
| 9.4 | Reading session (D3) | Put book on mat; turn knob; lift for 10 s, then lift for 35 s | OLED shows title/UID + page 1..999; 10 s lift keeps session and counts; 35 s lift logs `session ended, counts cleared` |
| 9.5 | Hold-to-record (D5, D1) | Hold RECORD 0.3 s, then 2 s, 6 s, 12 s | 0.3 s cancels; 2 s / 6 s stream 8192 B chunks with timer on OLED; 12 s cuts cleanly at 10 s (`320000` bytes); `CSV,reading,...` printed |
| 9.6 | Feedback + IPA + safety (D6, D7, D11) | Trigger grades 1, 2 (with wrong words), 3, 4 | Each command shows OLED text (IPA glyphs, never `?`), moves arm/tongue and stops pulses within 3 s; flipping servo 5 V toggle switch makes servos limp at once |
| 9.7 | Mistakes + teacher call (D8, D9) | Miss the same word/sound 5 times; hold STOP 2 s while proposal shows; press ACK on nest | `CSV,count` counts 1..5 (absent words unchanged); at 5 OLED shows `Call your teacher? Hold STOP`; 2 s hold sends `/v1/help` -> command 7 -> ACK -> command 8 + wave |
| 9.8 | Quiet mode (D10) | Short STOP press (< 2 s); stay quiet, then talk loudly 1 s; repeat within 30 s | OLED shows `Quiet mode`; quiet = nothing; 1 s loud = command 5 (`Shh` + gesture); second loud within 30 s = nothing |
| 9.9 | Dark room (D11b) | Cover nest BH1750 (< 10 lx for 3 s), also mid-recording; uncover (>= 15 lx for 3 s); unplug BH1750 | Within ~4 s OLED shows `Too dark: please turn on the light`, `CSV,dark,1,1`, inputs blocked, mid-recording cancelled; uncover restores within ~6 s; unplugged sensor never blocks |
| 9.10 | Recovery + 2 dolls (D12, D13) | Pull nest power mid-reading; kill Wi-Fi 2 min; power-cycle doll; read on `doll` and `doll_2` together | Recovers without hands; both dolls get independent grades and appear separately on dashboard |

Spoken phrases: `data_doll/phrases/*.wav` (already made; remake with `tools/make_phrases.py`).
Put them on each doll once: select env `doll` (or `doll_2`), then **Platform > Upload Filesystem Image**.
The doll envs pack `data_doll/`, not `data/`, so the books never fill the doll's flash.
Pass: the boot line shows `fs=1`, and every command in 9.6 speaks.

---

## Stage 10. Real nest firmware (`src/app/gateway/nest.cpp`)

Wiring: full nest wiring from `config/settings.h` (`gateway_pins`). Every part is optional.
Upload env: `nest` (or `nest_sh1106`). Monitor: `115200`.
`config/private.h`: Wi-Fi, `LUMPI_LAN_KEY`, `LUMPI_ADAPTER_URL "https://lumpi.vercel.app"`, `LUMPI_CLOUD_CA`.

### 10a. Laptop logic tests (no boards)

```bash
g++ -std=c++17 -Wall -Wextra -Werror -I src/common tests/test_nest_logic.cpp -o build/test_nest_logic && ./build/test_nest_logic
```

Pass: `0 failure(s)`. It also feeds the nest's reply into the doll's own parser.

### 10b. Boot

| # | Do | Pass when |
|---|---|---|
| 10.1 | Upload, open Monitor | `CA set yes`, `Free flash` over 920 KB, `CSV,boot,0,...`, then `Nest address http://<ip>` |
| 10.2 | Read `Flash write speed` | 64 KB/s or more. Under 64 = two dolls at once may lose chunks: tell Tanakit |
| 10.3 | Read `Largest free RAM block` | over 45000 |
| 10.4 | Wait 60 s | `CSV,beat,0,...` every 60 s; dashboard shows the nest's room data |

### 10c. Laptop plays a doll (no doll needed)

On a laptop on the same Wi-Fi. `NEST` = the nest address, `KEY` = the LAN key (type it, never paste it in chat).

```bash
read -rs KEY && export KEY NEST=http://<nest ip>
```

```bash
head -c 8192 /dev/zero > /tmp/chunk.bin
```

| # | Command (add `-H "Authorization: Bearer $KEY"` to each) | Pass when |
|---|---|---|
| 10.5 | `curl $NEST/v1/status` | JSON with 2 dolls |
| 10.6 | same, with `-H "Authorization: Bearer nope"` | 401 |
| 10.7 | `curl -X POST "$NEST/v1/start?node=1&attempt=1&passage=1&uid=04A1B2C3&fsr=2000"` | `ok` |
| 10.8 | `curl -X PUT --data-binary @/tmp/chunk.bin "$NEST/v1/chunk?node=1&attempt=1&index=0"` | `0` |
| 10.9 | the same chunk again | `0` again (resend: not stored twice) |
| 10.10 | same with `index=2` | 409 (piece 1 missing) |
| 10.11 | `curl -X POST "$NEST/v1/finish?node=1&attempt=1&chunks=1&bytes=8192"` | `assessing` |
| 10.12 | `curl "$NEST/v1/attempt?node=1&attempt=1"` every second | `pending`, then `result,3,0` or `result,4,0` (silence); monitor shows `GRADE` and `CSV,reading` |
| 10.13 | `curl "$NEST/v1/help?node=1"` | `waiting`; monitor `HELP`; Discord + dashboard popup |
| 10.14 | stand in front of the distance sensor | OLED shows `Doll 1: HELP n s` and `Press ACK = coming` |
| 10.15 | press ACK, ask 10.13 again | `coming`; monitor `CSV,ack,1,<s>` |
| 10.16 | cover the light sensor 4 s, then `curl -X POST "$NEST/v1/presence?node=1&present=1&fsr=2000&mode=read"` | `dark`; 10.7 with `attempt=2` -> 409 `dark: ...` |
| 10.17 | uncover 4 s, ask 10.16 again | `ok`; `CSV,dark,0,0,...` |
| 10.18 | unplug the light sensor, reboot | nothing is ever `dark` |
| 10.19 | `curl -X POST "$NEST/v1/presence?node=1&present=0&fsr=0&mode=quiet"`, then `curl -X POST "$NEST/v1/noise?node=1&db=80&peak=20000"` twice | `command,5`, then `ok` (30 s cooldown) |

### 10d. With real dolls

| # | Do | Pass when |
|---|---|---|
| 10.20 | doll 1 reads 2 s, 6 s, 12 s | each gets a grade; 12 s is cut at 10 s (`320000` bytes in `CSV,reading`) |
| 10.21 | both dolls read at the same moment | both graded, no mix-up; `CSV,reading` chunks refused = 0 |
| 10.22 | a wrong word (code 2) | doll shows the word and plays its clip; `CSV,reading` clips > 0 |
| 10.23 | turn off the hotspot's internet 5 min, then on | every event arrives once (`CSV,event` age column), dashboard fills the gap |
| 10.24 | pull the nest's power mid-reading | doll says try again; nest boots, `CSV,boot,0,poweron,...`, old events still sent |
| 10.25 | router off 6 min | nest restarts itself after 5 min; dolls recover |
| 10.26 | wrong `LUMPI_CLOUD_CA` | grades come back 4; monitor shows the cloud error. Put the right CA back |

