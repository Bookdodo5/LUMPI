# T06 Cloud work log

Owner: Tanakit
Task: Cloud raw sensor upload and reconnect test

## Current status

In progress. The raw room event is built and tested locally. No real broker has been contacted yet.

## Work completed

- Added safe empty defaults when `config/private.h` is absent.
- Kept real Wi-Fi and MQTT credentials outside Git.
- Added the 10-second room-event interval from the build guide.
- Added a timestamped `room` event containing the raw light value.
- Used JSON `null` instead of invalid JSON when the light reading fails.
- Rejected cloud configuration unless it uses `mqtts://` and has a CA certificate.
- Tested all event logic on the laptop without an ESP32.

## Next work

1. Add the isolated ESP32 upload program and compile it.
2. Add a laptop test for disconnect and reconnect behavior.
3. Add local storage and retrieval proof for received events.
4. Later, run the real broker test with private credentials and save the before/after logs.

## Evidence

| Check | Result |
| --- | --- |
| Timestamped room JSON | Passed locally |
| Failed light read becomes `null` | Passed locally |
| Plain `mqtt://` rejected | Passed locally |
| Missing CA rejected | Passed locally |
| Credentials absent from Git | Passed |
| Real MQTT upload | Pending |
| Real reconnect | Pending |
