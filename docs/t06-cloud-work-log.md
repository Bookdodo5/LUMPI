# T06 Cloud work log

Owner: Tanakit
Task: Cloud raw sensor upload and reconnect test

## Current status

Software complete. The real broker and ESP32 reconnect test is still pending.

## Work completed

- Added safe empty defaults when `config/private.h` is absent.
- Kept real Wi-Fi and MQTT credentials outside Git.
- Added the 10-second room-event interval from the build guide.
- Added a timestamped `room` event containing the raw light value.
- Used JSON `null` instead of invalid JSON when the light reading fails.
- Rejected cloud configuration unless it uses `mqtts://` and has a CA certificate.
- Tested all event logic on the laptop without an ESP32.
- Added the isolated `p3_cloud` ESP32 upload program.
- Added Wi-Fi, NTP, TLS MQTT, QoS 1 publishing and automatic reconnect logs.
- Tested the disconnected and reconnected scheduling decisions on the laptop.
- Compiled the complete `p3_cloud` ESP32 firmware successfully.
- Added the laptop cloud worker that validates and stores MQTT events in SQLite.
- Added timestamped record retrieval and duplicate-ID tests.
- Tested that the worker subscribes again after a simulated reconnect.

## Next work

1. Later, run the real broker test with private credentials and save the before/after logs.

## Evidence

| Check | Result |
| --- | --- |
| Timestamped room JSON | Passed locally |
| Failed light read becomes `null` | Passed locally |
| Plain `mqtt://` rejected | Passed locally |
| Missing CA rejected | Passed locally |
| Disconnect blocks upload | Passed locally |
| Reconnect resumes upload scheduling | Passed locally |
| `p3_cloud` ESP32 build | Passed |
| SQLite storage and retrieval | Passed locally |
| Duplicate event handling | Passed locally |
| Worker reconnect subscription | Passed locally |
| Credentials absent from Git | Passed |
| Real MQTT upload | Pending |
| Real reconnect | Pending |
