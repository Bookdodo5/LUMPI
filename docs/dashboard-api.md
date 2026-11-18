# LUMPI dashboard API

The laptop server `laptop/dashboard_server.py` (port 8080) takes messages from
the nest and the laptop AI, and serves the teacher dashboard.
The reference examples are `EXAMPLES` in that file. If this page and the code
disagree, the code wins. Update both in the same commit.

## Sending: POST /api/telemetry

Every sender (nest, laptop AI) uses this one route.

- Header `X-LUMPI-Token: <LUMPI_LAN_KEY>` (same key as `config/private.h`).
  Wrong or missing key: `401`.
- Body: one JSON object, at most 8192 bytes. Too big: `400`.
- Reply: `200 {"result": "added"}`, or `"duplicate"` when the `event_id` was
  already seen (safe to resend). A bad message: `400 {"error": "<reason>"}`. The reason
  and a fix hint also show on `/debug/`.

Every message has these six fields:

| Field | Type | Rule |
|---|---|---|
| `event_id` | text | `<sender id>/<number>`, under 96 characters, unique. Resending the same id is ignored. |
| `node_id` | whole number | `0` = nest, `1`-`3` = dolls |
| `event_type` | text | `room`, `start`, `status`, `score`, `help`, `noise` |
| `recorded_at` | whole number | unix seconds when it happened (NTP time), after 2023-11-14 |
| `activity` | text or null | `idle`, `book_on`, `recording`, `waiting_ai`, `feedback`, `quiet_study`, `stopped`, or `null` |
| `data` | object | fields per event type below. Plain values only (number, text, null), except `score.words`. |

Use `null` for a sensor that failed. Never send 0 for one.

### room (nest, node 0, every 10 s)

```json
{"event_id":"lumpi_1a2b3c4d/42","node_id":0,"event_type":"room","recorded_at":1794794400,
 "activity":null,"data":{"lux":230.5,"distance_cm":160.0}}
```
`lux` = light at the nest. `distance_cm` = teacher distance from the nest.
The server saves one reading a minute for the past-class charts.

### start and status (doll, via the nest)

`start` when a book goes on the mat. `status` is the heartbeat, every 10 s.
Both carry the full doll state:

```json
{"event_id":"lumpi_1a2b3c4d/43","node_id":1,"event_type":"start","recorded_at":1794794401,
 "activity":"book_on","data":{"uid":"04A1B2C3","passage":2,"attempt":1,"fsr":3100}}
```
With no book: `"uid":null,"passage":null,"attempt":null`.

Reading session: begins when `activity` changes to `book_on`, `recording`,
`waiting_ai` or `feedback`, and ends at any other activity. The server
works this out from these messages. No sender has to send a session id.

### score (laptop AI, one per reading attempt)

```json
{"event_id":"laptop_ai/17","node_id":1,"event_type":"score","recorded_at":1794794410,
 "activity":null,
 "data":{"uid":"04A1B2C3","passage":2,"attempt":1,"score":72,
  "words":[
   {"word":"think","score":55,"phonemes":[{"ipa":"θ","score":30},{"ipa":"ɪ","score":80},{"ipa":"ŋ","score":60},{"ipa":"k","score":50}]},
   {"word":"fish","score":90,"phonemes":[{"ipa":"f","score":95},{"ipa":"ɪ","score":85},{"ipa":"ʃ","score":90}]}
  ]}}
```
- `node_id` = the doll that recorded it. `uid`, `passage`, `attempt` = same values as the nest's `/v1/assess`.
- `score`, every word `score` and every phoneme `score`: numbers 0-100.
- `words`: every word of the passage, in reading order, at most 80.
- `ipa`: one IPA sound of that word, as text (e.g. `θ`, `iː`). Send the JSON as UTF-8.
- Saved to `data/scores/<day>.jsonl`. The dashboard groups these by sound and by word, and uses the weak sounds to show why a word was wrong.

### help (doll, via the nest)

The child pressed "need help". Sending it shows a popup on the dashboard and marks the doll
"Needs help" for 5 minutes.
```json
{"event_id":"lumpi_1a2b3c4d/45","node_id":1,"event_type":"help","recorded_at":1794794420,"activity":null,"data":{}}
```

### noise (doll, via the nest, Quiet Study only)

```json
{"event_id":"lumpi_1a2b3c4d/46","node_id":2,"event_type":"noise","recorded_at":1794794430,"activity":null,"data":{"db":68.5}}
```
The bands on the page (60 / 70 dB) are placeholders until the mic is calibrated.

## Reading: GET (used by the dashboard pages)

| Route | Returns |
|---|---|
| `/api/dashboard/nodes` | latest state of every node: `status` (`online` under 30 s, `stale`, `offline` after 60 s, `unknown`), `activity`, `values`, `help_at`, `help_age_s`, `noise_db` |
| `/api/dashboard/history?minutes=5\|15\|60\|180` | messages per node in that window (in memory only) |
| `/api/dashboard/scores?day=today` | today's score lines and room readings |
| `/api/dashboard/scores?day=past` | every saved day: `days`, `scores`, `room` |
| `/api/dashboard/events`, `/api/dashboard/errors` | last messages, and rejected messages with hints (developer page) |

A saved score line = the `score` data plus `node_id`, `recorded_at` and
`session` (the `recorded_at` when that reading session began).

## Testing without hardware

```
LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py
LUMPI_LAN_KEY=test python3 laptop/mock_class.py --seed 7 --days 4
python3 -m unittest tests.test_mock_class tests.test_dashboard_server
```
`mock_class.py` sends every message type above through this route, with the
token. A new `--seed` gives new numbers.
