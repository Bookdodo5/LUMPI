# T07 Cloud pipeline work log

Owner: Tapanan (integration)
Task: node -> gateway -> cloud -> dashboard, one source of truth.
Branch: `cloud-supabase-vercel` (not merged, not deployed to Vercel yet).

## Current status (2026-11-19; firmware update 2026-11-22 below)

Cloud side works end to end with stand-ins. Not yet connected to real dolls or the nest.

```
Doll --HTTP, local Wi-Fi--> Nest --HTTPS + LAN key--> Vercel api/index.py --> Supabase (events table)
                             Nest --HTTPS /v1/assess--> Vercel --> Runpod AI (ai/app.py)
Teacher browser --HTTPS + password--> Vercel --> reads Supabase
```

| Part | Where | State |
| --- | --- | --- |
| Database | Supabase project `ooumsmvtdplnoiggxypg` (Singapore), tables `events`, `rejects` | Live, empty. Migration `supabase/migrations/20261119000000_events.sql` applied by hand in the SQL editor |
| Cloud API | `api/index.py` + `vercel.json` (region `sin1`) | Tested on the laptop against real Supabase. Not deployed |
| AI | Runpod endpoint `y1w5ll6hvg8v5d` (`ai/app.py`), cpu3c 8 vCPU, AP-JP-1 then US-CA-2 | Deployed, **max workers 0** (off). Placeholder scoring |
| Dashboard | `dashboard/v7` (unchanged), served by Vercel | Not deployed |
| MQTT / HiveMQ | `.old/2026-11-22-bench/p3_cloud.cpp`, `.old/2026-11-26-tanakit/adapter/cloud_worker.py` | Not used any more. HTTPS covers M5, E4, E5 |

## Decisions

- One event format: the dashboard event (`docs/dashboard-api.md`). One row per `event_id`; a resend is a duplicate, not a new row.
- Oracle dropped: no free Arm capacity, card problems. Plan B: Supabase + Vercel + Runpod.
- MQTT dropped: needs an always-on subscriber we do not have. Course spec only gives MQTT as an example.
- AI runs float32, 8 torch threads (set in the request too; `os.cpu_count()` is 256 on Runpod hosts).
- Runpod model cache (`runpod.Model`) is GPU-only. The CPU worker downloads the model at start.

## Measurements

Cloud API on the laptop -> Supabase: event stored 259 ms mean; `/v1/assess` with mock AI 1346 ms mean (before Singapore region and parallel lookup).

Runpod AI, timed from Thailand, worker in US-CA-2, float32, 8 threads:

| Reading | Model | Total |
| --- | --- | --- |
| 3 s | 454-483 ms | 1830 ms mean |
| 8 s | 1135-1160 ms | 2731 ms mean |
| Cold start | - | 36-45 s |

Estimated whole reading (adds nest upload + Vercel): 2.3-2.8 s for 3 s, 3.2-4.2 s for 8 s. Target 4 s.

## Tests

```bash
python3 -m unittest tests.test_cloud_api tests.test_dashboard_server tests.test_mock_class
set -a; source ~/.lumpi-supabase.env; set +a
python3 tests/pipeline_check.py && python3 tests/pipeline_check.py --cleanup   # real Supabase, 15/15 passed
python3 tests/runpod_latency.py y1w5ll6hvg8v5d   # costs ~$0.05; switch workers on first
```

Secrets live outside git: `~/.lumpi-supabase.env` (Supabase URL + keys), `~/.runpod/config.toml` (Runpod key).

## Switch the AI on and off

```bash
rp endpoint update y1w5ll6hvg8v5d --max-workers 1 --min-workers 1   # warm for class/demo, $0.288/h
rp endpoint update y1w5ll6hvg8v5d --min-workers 0 --max-workers 0   # off, $0
```

## Next

1. **AI (2026-11-26, ready, NOT deployed):** the Runpod worker `ai/app.py` now only **transcribes**
   (owner: "it should only transcribe, no grading"): it answers `{"phones": [...], "timing_ms": {...}}`, the
   notebook's `sound_to_ipa` method (audio normalized like AutoProcessor, vocab.json instead of espeak, float32
   only). **Grading is in the cloud API** (`api/index.py` `grade()`: notebook `norm`/`align`/`score_words` against
   `config/books.json`). Next: deploy (`rp flash deploy ai`, check Runpod stock first, workers back to 0 after),
   run `tests/runpod_latency.py`, then 8c in `docs/testing-steps.md`.
2. Commit this branch, merge, import the repo on vercel.com, set env vars: `SUPABASE_URL`, `SUPABASE_SECRET_KEY`, `LUMPI_LAN_KEY`, `LUMPI_DASHBOARD_PASSWORD`, `LUMPI_AI_URL`, `LUMPI_AI_KEY`.
3. Check Vercel passes `?node&attempt...` through the `/v1/assess` rewrite, and that the browser shows a password box.
4. Nest firmware: one sketch that sends dashboard events + `/v1/assess` over HTTPS, with the flash outbox (E4).
5. Doll firmware: stop recording on silence (shorter uploads, faster grading).
6. ~~Move `adapter/cloud_worker.py` to `.old/`~~ done 2026-11-26 (see "Gateway helper cleanup" below).

## Link firmware built (2026-11-22)

Every test stage now has firmware. Step-by-step tests: `docs/testing-steps.md`.

| Piece | File | State |
| --- | --- | --- |
| Doll -> nest -> cloud, nest side | `src/bench/5_doll_nest/5_nest.cpp` (env `s5_nest`) | Compiles. Not run on hardware |
| Doll side | `src/bench/5_doll_nest/5_doll.cpp` (env `s5_doll`, `s5_doll_2`) | Compiles. Not run on hardware |
| Laptop stand-in doll | `tests/fake_doll.py` | Written. Needs a running nest |
| http + https helper | `src/drivers/lumpi_net.h` | Used by `link_*` and `s6_nest_to_dashboard` |
| Status / help / noise events | `src/common/dashboard_event.h` | Laptop tests pass; server accepts all 7 event types |

Bench limits (marked `ponytail:` in code): one recording at a time on the nest, 3 s each (96 kB buffer);
16-event outbox in memory (no flash yet); noise dB uncalibrated; feedback is printed, not acted out.
All pins now come from `config/settings.h` (+ `doll_tuning::book_on_fsr`, calibrate with env `s1b_pressure`).

## Bench reorganised (2026-11-22)

`src/bench/` is now in stage order (`0_setup/` ... `6_nest_cloud/`, `extras/`); files and environments carry the stage number (`s1a_buttons` ...). See README and `docs/testing-steps.md`.
Retired to `.old/2026-11-22-bench/`: `p2_i2c_scan`, `p2_light`, `p2_distance`, `p3_cloud`, `sensor_5_hcsr501_motion` (and their environments).

## Lessons

- Runpod: a worker that crashes at start restarts and bills. `tests/runpod_latency.py` stops workers if a job stays queued 3 min.
- Check Runpod stock right before deploys and demos (`rp catalog --cpu --serverless --vcpu 8`). Japan CPU stock was zero today.
- Run the worker's start-up locally before each 10-minute rebuild.

### Readable rewrite of the link firmware (2026-11-22)
- Goal: a newcomer understands each file cold. Checked by a fresh agent with no context (scores 7-9/10, findings fixed).
- `src/drivers/tiny_http.h` (new): the hand-written request reader, out of the nest sketch.
- `5_nest.cpp`: glossary + message walk-through at the top; one function per route returning a `Reply`; std::deque event queue.
- `dashboard_event.h`: one `event()` builder; `room/start/status/help/noise` return the JSON text ("" = refused). UIDs must be uppercase hex.
- Bug fixed: the doll gave up on `/v1/attempt` after 5 s while the nest can wait 12 s for the cloud; now 15 s per ask, and no answer = ask again.
- Builds: s5_nest, s5_doll, s5_doll_2, s6_nest_to_dashboard. Laptop: test_dashboard_event passes; all 7 events accepted by dashboard_server.check_event.

### Stage 5 passed on hardware (2026-11-23)
- Doll board (COM10) -> nest board (COM9) over Wi-Fi "Biotin B7", no cloud (LUMPI_ADAPTER_URL ""):
  3.0 s recording, 47 chunks / 96000 bytes uploaded in 3.4 s, grade back 3519 ms after recording (goal 4000). PASS.
- tests/fake_doll.py from the laptop: all checks passed (upload 5.09 s from Windows Python).
- Fixes found on the way: mic channel is I2S_CHANNEL_FMT_ONLY_RIGHT with L/R on GND (this core swaps
  left/right); doll re-sends a stuck chunk after 1.5 s, 5 tries (the hotspot Wi-Fi drops packets).
- Speaker blocked: both MAX98357A boards damaged by re-soldering (LRC pad shorts to GND when pressed). Pre-soldered amps ordered.

### Stage 7 passed on hardware (2026-11-23)
- Doll -> nest -> laptop dashboard (Windows Python, http://10.61.117.202:8080): room, start and status events all
  accepted (0 rejected). Dashboard shows nest online (lux, distance) and doll 1 online with uid, passage, attempt.
- Grade 4 "uncertain" as expected (the laptop dashboard has no AI). Reading total 4704 ms: upload 3919 ms is
  most of it (one TCP connection per chunk); speed-up = one kept-alive connection for all chunks (not done yet).

### Gateway helper cleanup (2026-11-26)
Tanakit's small `src/common/gateway_*` headers folded into the real nest firmware; the dead MQTT route retired.
Old files kept in `.old/2026-11-26-tanakit/` (same relative paths).
- Distance: the HC-SR04 code was copy-pasted in `5_nest.cpp` and `6_nest_to_dashboard.cpp`. Now one
  `hardware::read_distance_cm(unsigned long* echo_us = nullptr)` in `src/drivers/real_io.cpp` (NAN = no echo,
  whole cm = echo us / 58, same as before). Sketches still set `pinMode` for trig/echo in `setup()`.
  `gateway_distance_logic.h` + its test retired.
- Light: BH1750 address and `raw / 1.2` inlined into `real_io.cpp` (its only user). `gateway_sensor_logic.h` + test retired.
- Query parser: `gateway_start_packet.h` shrunk to `find_value` + `read_number` and renamed `src/common/query.h`
  (namespace `query`), used by `tiny_http.h`. Test: `tests/test_query.cpp`. The start-packet parser was MQTT-era only.
- MQTT retired: `gateway_cloud_logic.h`, `adapter/cloud_worker.py`, `adapter/requirements.txt`,
  `tests/test_cloud_worker.py`, `tests/test_t06_cloud_contract.py`, `tests/emit_gateway_cloud_event.cpp`,
  `tests/test_gateway_cloud_logic.cpp` (`adapter/` is gone).
- Checked: clang `-Wall -Wextra -Werror` test_query, test_dashboard_event, test_quiet_study_logic,
  test_round_trip all pass. Python `test_cloud_api test_dashboard_server test_mock_class`: 39 OK (1 skipped).
  `pio run` s2b_light, s2c_distance, s4_nest_check, s5_nest, s5_doll, s6_nest_to_dashboard: all SUCCESS.
  Not yet run on hardware.

## Transcribe-only AI, grading in the cloud API (2026-11-26)

- `ai/app.py`: answers only the sounds heard (CTC merge repeats, drop `<pad>`/`<s>`/`</s>`/`<unk>`), no score.
  Audio is normalized (zero mean, unit variance) like the notebook's AutoProcessor; the old worker skipped this.
  Under 0.1 s of audio: `[]`, no crash. int8 copy and `precision` removed. Checked locally (stubbed `runpod`,
  CPU torch): heard phones identical to the notebook method on a Piper TTS "Three green trees." sample.
- `api/index.py`: looks up the book (`load_book`), asks the AI, grades: unknown book/passage or AI failure
  `4,0`; nothing heard `3,0`; every word >= 60 `1,0`; else `2,<worst word>`. Word score = right pairs / all pairs.
  Each stored word keeps every (expected, heard) pair: `phonemes` items get a `heard` key, and a `pairs` list
  also holds extra sounds (`[null, "b"]`). Passages 1-999 accepted.
- New route `GET /v1/word_audio?uid&passage&word` (LAN key): the clip from `load_clip` (`data/word_audio/<language>/<word>.wav`).
  `load_book` / `load_clip` are the only readers, so books can move to Supabase Storage later.
- `tests/mock_ai.py` answers phones too; `tests/fixtures/books.json` is the test book (uid FFFFFFFF).

## Transcribe-only AI deployed to Runpod (2026-11-26, endpoint y1w5ll6hvg8v5d, v6)

- Deploy: `TMPDIR=~/.cache/lumpi-flash-tmp rp flash deploy ai --python-version 3.12` (450 s). Without TMPDIR it fails: WSL `/tmp` (3.7 GB RAM disk) fills up.
- The first job after the deploy hit an old-code worker (`missing ... 'uid', 'passage', 'attempt'`); a retry on a fresh worker worked.
- Real speech (Piper, US voice): "Three green trees" -> grade `1,0` score 100; "Free green cheese" -> `2,2` score 72 (th->f and tr->ch found). Graded by `api/index.py` on the laptop.
- Timed from Thailand, worker in US-CA-2: cold start 34-42 s; warm 3 s reading 2445 ms total (model 547 ms), 8 s reading 3633 ms (model 1267 ms).
- Switched off after the test: workers 0-0. Balance $9.18 -> $9.17 (billing may lag).
- Not tested: Vercel -> Runpod (API not deployed), British (en-gb) books, a real child's voice.

## Review fixes, British base, richer /v1/assess answer (2026-11-26)

- Review fixes in `api/index.py`: one word limit (80, `book_store.MAX_WORDS` = `dashboard_server.MAX_WORDS`);
  the AI gets what is left of 11 s from the request's arrival; Storage/database calls on that path time out after 3 s;
  books cached 60 s; a database failure after grading still answers the grade; non-ASCII keys give 401, not 500;
  `/v1/word_audio` also takes the dashboard password and writes no reject row; a malformed stored book gives 502.
  `tools/add_book.py` refuses a word with no sounds.
- **British English is the base** (owner): `en-gb` everywhere; voice `en_GB-jenny_dioco-medium`. `norm()` now lives
  in `laptop/book_store.py`, shared by the grader and `add_book.py`: r-coloured/fused model tokens become British
  vowels (`ɑːɹ` -> `ɑ`, `ɚ` -> `ə`, `ɪɹ` -> `ɪ ə`), Mandarin spellings/tone numbers the model sometimes writes are mapped,
  length marks are dropped (British vowel pairs already differ in letter). Test book `00000000FFFF` re-uploaded as en-gb.
- Measured (22 words, Piper clip -> the model -> grade): correct British clips pass about 14/22 (jenny_dioco, alba);
  other British voices 2-14. The stored test-book clips: 5/7. The model is weak on single British words, so expect
  some correct readings to be graded "retry"; check with a real child's reading before tuning further.
- `/v1/assess` answer: line 1 stays `code,word`; for code 2 one more line per wrong word:
  `<index>,<word>,<book sounds>,/v1/word_audio?uid=..&passage=..&word=<index>`. Firmware is out of scope for now (owner).

## Vercel project connected (2026-11-26)

- Vercel project `lumpi` linked to GitHub `Bookdodo5/LUMPI`, production branch `main`. No deployment yet.
- The owner commits and merges. `main` has no `.vercelignore` yet: a push to `main` without it would publish the whole repo.
- Settings to import in Vercel (Project > Settings > Environment Variables): `C:\Users\PC\lumpi-vercel.env` (secrets, outside the repo).
- Open: `LUMPI_LAN_KEY` is only 7 characters; replace it with a long random key. British grading: correct test clips pass about 14 of 22 words.

## MVP trim: laptop server retired, cloud = two files (2026-11-26)

- Owner: minimum viable product; every line LUMPI logic, checks only where needed. Old state copied first to
  `.old/2026-11-26-pre-mvp/`; retired files moved to its `retired/` (nothing deleted).
- Cloud code now: `api/lumpi.py` (the logic, story order; Vercel entry, class `handler`) and `api/_plumbing.py`
  (web server, passwords, Supabase, AI call, checks). Read `docs/cloud-code-guide.md`. `vercel.json` points to
  `api/lumpi.py`. `laptop/` is gone; retired with it: laptop dashboard server, mock class, replay, fake doll,
  mock AI, pipeline check, Runpod latency script, old tests, `tools/capture_dashboard.js`, `dashboard/v1`-`v6`
  (and `/debug`), `tests/prompt-test/`.
- Cut: rejects table logging, `/api/dashboard/events|errors`, gzip, book cache, `LUMPI_BOOKS`, `LUMPI_TZ`,
  `X-LUMPI-Token` (the nest also sends `Authorization: Bearer`), score `pairs` (now `extra` = sounds heard but not in
  the book; `phonemes[].heard` keeps wrong sounds), per-field event checks and their hint texts.
- Behaviour changed: `/api/telemetry` now refuses only events missing the six fields or with wrong types (no node/type
  pairing, clock, activity, data-field or word-list item checks: such events are now stored); `/v1/assess`: every outside
  call shares the nest's 11 s budget, the AI gets it minus 2 s kept for storing; UID must be 8-20 hex (`400`, was `4,0`);
  page `01` accepted; bad `minutes`/`day` give `404`; `/v1/page` with a malformed stored book gives `404` (was `502`);
  nodes view drops unused fields (`counts`, `noise_db`, ...); `tools/add_book.py` no longer checks title length, the
  3000-character page limit or a stored book's language (only `en-gb` exists).
- Proven: old vs new on the same mock class (2422 valid messages, duplicates, and bad inputs both refuse): identical
  accept/reject, stored rows, nodes/history/scores views (fields v7 reads) at 5 times; grading identical on 4000
  random cases; assess/page/word_audio identical apart from the changes above. v7 rendered headless against the new
  API with no console errors. Live Supabase (test rows, cleaned up): telemetry, duplicate, views, page, clip, assess
  store all worked. Tests: `python3 -m unittest tests.test_cloud tests.test_add_book`.
- Not verified: a real Vercel deploy (that `api/_plumbing.py` is bundled, found on the path and not served).

## 2026-11-26: two full books (OCR)

- `tools/ocr_to_book.py` writes `data/books/book<N>.{full,upload}.json` (git-ignored, copyrighted text). UIDs 00000001 (Minecraft: The Island, 268 pages) and 00000002 (Satan, Cantor, and Infinity, 259 pages) are placeholders.
- Page limits raised: 500 words (now in `tools/add_book.py` `split_words()`; the cloud no longer limits words). Recording is still 8 s (`MAX_RECORDING_BYTES`, firmware `Session::max_bytes`), not enough for a full page.
- Upload job: `systemctl --user status lumpi-upload3`, log `data/books/upload.log`. Safe to rerun.
- Not deployed by this step.
- 2026-11-26 after review: gzip database replies; a refused event answers 400 (not 503); silent-node query skips score rows; `.old/` git-ignored. 9 tests OK.
