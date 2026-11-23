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
| MQTT / HiveMQ | `.old/2026-11-22-bench/p3_cloud.cpp`, `adapter/cloud_worker.py` | Not used any more. HTTPS covers M5, E4, E5 |

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

1. Real scoring in `ai/app.py` (forced alignment from `reading_buddy_live_reading_v2.ipynb`), then redeploy (float32 default not deployed yet).
2. Commit this branch, merge, import the repo on vercel.com, set env vars: `SUPABASE_URL`, `SUPABASE_SECRET_KEY`, `LUMPI_LAN_KEY`, `LUMPI_DASHBOARD_PASSWORD`, `LUMPI_AI_URL`, `LUMPI_AI_KEY`.
3. Check Vercel passes `?node&attempt...` through the `/v1/assess` rewrite, and that the browser shows a password box.
4. Nest firmware: one sketch that sends dashboard events + `/v1/assess` over HTTPS, with the flash outbox (E4).
5. Doll firmware: stop recording on silence (shorter uploads, faster grading).
6. Move `adapter/cloud_worker.py` to `.old/` once the team agrees (`p3_cloud.cpp` moved 2026-11-22).

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
