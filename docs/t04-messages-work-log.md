# T04 Messages work log

Owner: Tapanan
Task: Define messages, session state and audio route

## Current status

The message contract and per-doll session rules are code. The laptop round-trip
test passes (23/23). Not yet tested between real boards: that needs
`config/private.h` (Wi-Fi page) and a running gateway (T03).

## Work completed

- `src/common/messages.h`: protocol version 1 (the `/v1` prefix), the 10 nest
  routes, the 2 laptop routes, command codes 1–8 and the audio format.
  Source: [Message format](https://lumpi-build-guide.vercel.app/reference/messages).
- `src/common/session.h`: one `Session` per doll (IDLE, RECORDING, ASSESSING,
  RESULT), ordered audio pieces, totals check, stored result, time-outs and
  `HelpState`. Source: [The referee](https://lumpi-build-guide.vercel.app/gateway/rules).
- `src/test/test_round_trip.cpp`: one reading from start to result on the laptop,
  with two dolls at once.

Run the test from this folder:

```sh
"/mnt/c/Program Files/LLVM/bin/clang++.exe" -std=c++17 -Wall -Wextra -I src/common src/test/test_round_trip.cpp -o build/test_round_trip.exe
./build/test_round_trip.exe
```

## Audio route

Doll records 16 kHz, 16-bit mono → `PUT /v1/chunk` pieces of up to 2048 bytes
(the reply is the ACK) → `POST /v1/finish` with the totals → the nest sends the
whole recording to the laptop with `POST /v1/assess` → reply `code,word` → the
nest stores the command → the doll asks `GET /v1/attempt` until it gets
`result,code,word`.

## Example packets

All local messages carry `Authorization: Bearer <LUMPI_LAN_KEY>`.

| Step | Request | Reply |
| --- | --- | --- |
| Start | `POST /v1/start?node=1&attempt=5&passage=2&uid=04A1B2C3&fsr=3100` | `200 ok` |
| Piece 0 | `PUT /v1/chunk?node=1&attempt=5&index=0` + 2048 bytes | `200 0` |
| Finish | `POST /v1/finish?node=1&attempt=5&chunks=2&bytes=4096` | `200 assessing` |
| To laptop | `POST /v1/assess?node=1&attempt=5&passage=2&uid=04A1B2C3` + recording | `200 2,1` |
| Result? | `GET /v1/attempt?node=1&attempt=5` | `200 result,2,1` |
| Noise | `POST /v1/noise?node=1&db=70` | `200 command,5` |
| Cloud | `lumpi/<gateway id>/events` | `{"id":"lumpi_1a2b3c4d/482913/7","body":{"kind":"assessment","node":1,"attempt":5,...}}` |

## Notes for the team

- Gateway: include `messages.h` in `gateway.h` and use `lumpi::Command`; do not
  type the codes again (the guide's plan page lists them in `gateway.h`).
- Test Lead: the full rules tests (`src/test/test_session.cpp`, rules 1–32) are
  still yours; this file is only the T04 round trip.
- Laptop: `reading_ai.py` codes 1–4 must match `messages.h`.

## Next work

1. Create `config/private.h` and run the Wi-Fi test on every board.
2. Repeat this round trip between a real doll and the gateway, and save the
   Serial logs here as evidence.

## Problems and fixes

- None recorded yet.
