# T08 Nest firmware plan (Tanakit)

Source: `nest-firmware-prompt.txt`.

## Status 2026-11-27

- Written: `nest.cpp`, `nest_parts.h`, `src/common/nest_logic.h`, `tests/test_nest_logic.cpp`, envs `nest` / `nest_sh1106`, stage 10 in `docs/testing-steps.md`.
- Compiles with `-Wall -Wextra`, no warnings in our code. 73% of the app slot. All 36 envs build except `doll*` (below). **Not run on hardware.**
- Laptop tests: all 6 C++ suites pass; `test_nest_logic` also feeds the nest reply into the doll's own parser.
- Core confirmed: `espressif32@7.1.3` = Arduino 2.0.17.
- Doll (`41a65af`) does not link: `inline void IRAM_ATTR` in `doll_parts.h` lines 153, 162, 163 ("dangerous relocation"). Changing `inline` to `static` on those 3 lines builds. Not changed: Thianrawit's file.
- Build here needs fake settings (no `config/private.h` on this PC), or the compiler drops everything after Wi-Fi start:
  `PLATFORMIO_BUILD_FLAGS='-DLUMPI_WIFI_SSID=\"t\" -DLUMPI_WIFI_PASSWORD=\"t\" -DLUMPI_LAN_KEY=\"k\" -DLUMPI_ADAPTER_URL=\"https://example.invalid\"' pio run -e nest`
- Defaults taken (owner did not answer yet): keep uploading `distance_cm`; OLED on when distance < 40 cm.
- Not built: N12 `/v1/book` (404, doll ignores it), N13 OTA.
- Review fixes: delete a recording before rewriting it and after grading (flash is 304 blocks, 2 recordings at once need 158); outbox cap 64 -> 40; outbox no longer deletes an unsent batch when full; watchdog 90 s.
- Watch in class: outbox speed (each event = one new TLS connection; `CSV,event` age column), and the other doll's chunk retries while a clip is being sent.
- Doll-side guess from review: `poll_attempt` keeps a ~4.8 KB `AttemptReply` on the 8 KB net_task stack (`doll_parts.h` ~499): make it `static`.
- Unchecked: real word clip sizes (nest refuses clips over 48 KB) and that they have a plain 44-byte WAV header.

## Files

| File | What | Lines (estimate) |
|---|---|---|
| `src/app/gateway/nest.cpp` | THE STORY: glossary, 3 tasks, one function per route, grading, room loop | ~450 |
| `src/app/gateway/nest_parts.h` | PLUMBING: LittleFS recordings/outbox/clips, binary reply, OLED, ACK ISR, Wi-Fi watchdog, CSV | ~250 |
| `src/drivers/lumpi_net.h` | add `request()` that sends a `Stream*` (a File) instead of a RAM buffer | ~15 |
| `src/drivers/tiny_http.h` | add a binary reply (WAV clip) | ~10 |
| `platformio.ini` | add env `nest` (+ `nest_sh1106`) | ~10 |
| `docs/testing-steps.md` | stage 9 nest tests | ~40 |

Shared headers (`session.h`, `messages.h`, `dashboard_event.h`, `settings.h`, `tiny_http.h` max_body) were already changed by the doll commit `41a65af`. Check only, no edits.

## Tasks (FreeRTOS)

| Task | Priority | Does | Never |
|---|---|---|---|
| http | 3 (high) | accepts dolls, reads one request, answers, appends chunks to flash | waits for the cloud |
| room | 2 | every 1 s: light, distance, DarkState, ACK, OLED, timeouts; every 10 s: room/status events | network calls |
| cloud | 1 (low) | grade queue first (stream file to `/v1/assess`, fetch 3 clips), then outbox | holds the dolls mutex during network |

- Queue `to_grade` (http -> cloud): node number.
- Task notification: ACK ISR -> room task.
- One mutex around `dolls[]`. Never held during network or flash I/O.
- All `WiFiServer`/client socket work stays in the http task (no source says it is safe across tasks).

## Build stages (compile after each; "compiles, not run on hardware" until tested)

| Stage | Features | Test on hardware |
|---|---|---|
| 0 | install PlatformIO here, build `doll` + `s5_nest` as they are | both compile |
| 1 | tiny flash speed check (append 8 KB blocks, print KB/s) | numbers decide stage 2 storage |
| 2 | N1 tasks + N3 routes + N2 recordings in flash + grading | 1 doll reads; then 2 dolls at once, no mix-up |
| 3 | N4 dark, N6 help + ACK, N7 noise, N8 OLED + room | cover sensor, call teacher, press ACK |
| 4 | N9 outbox (64 files) + events | Wi-Fi off 5 min, back: all events once |
| 5 | N5 word clips + forward S/W/P with clip marks | code 2 plays clip on doll |
| 6 | N10 TLS CA + N11 watchdogs, reconnect, CSV lines | wrong CA refused; pull power, router off/on |
| skip | N12 `/v1/book` (doll ignores non-200), N13 OTA (Tapanan) | |

## Risks found (web search + code reading)

1. **Two dolls at once may be faster than LittleFS.** Two dolls send 2 x 32 KB/s = 64 KB/s. Published LittleFS write speeds on ESP32 are about 55-64 KB/s. Stage 1 measures it. Fallback: a nest-only partition file with two raw 320 KB slots (`esp_partition_write`, erased ahead), no file system for audio.
2. **Flash writes pause the whole chip** (cache off, other tasks and non-IRAM interrupts wait). Keep each write one chunk; measure the doll's chunk ACK time (doll timeout 1500 ms).
3. **Cloud still takes only 256000 bytes** (`MAX_RECORDING_BYTES`) and 11 s. A 10 s reading (320000) gets 400 -> doll gets 4. Cloud change #1 must ship before the demo.
4. **Cloud still sends the old code-2 lines**, not S/W/P. The doll's parser reads only S/W/P, so code 2 shows no word until cloud change #3 ships.
5. **TLS root:** `lumpi.vercel.app` is signed by Google Trust Services (WR1 -> GTS Root R1, cross-signed by GlobalSign Root CA), not Let's Encrypt. Put GTS Root R1 + GlobalSign Root CA in `LUMPI_CLOUD_CA` so a Vercel chain change does not lock the nest out.
6. **TLS needs ~40-50 KB free heap in one block** at the handshake. Print the largest free block at boot and in the CSV beat.
7. **Core version unverified:** `espressif32@7.1.3` most likely bundles Arduino 2.0.17 (ESP-IDF 4.4). The doll uses the 2.x watchdog API. Stage 0 confirms.

## Questions for the owner

1. Who does cloud changes #1 and #3 (risks 3 and 4)?
2. Teacher distance: keep uploading `distance_cm` (dashboard charts it) / send null / send near-far only?
3. `room::teacher_margin_cm` (40) = "teacher nearer than 40 cm turns the OLED on". Correct?
4. Replace the 7-character `LUMPI_LAN_KEY` before the demo (ops).
5. Warm the AI before class (cold start 34-45 s gives "4,0") (ops).
