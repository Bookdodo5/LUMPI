# T06 Cloud work log

Owner: Tanakit
Task: Cloud raw sensor upload and reconnect test

## Current status

Software complete. The real broker and ESP32 reconnect test is still pending.

## Why the software works as intended

The Excel task and the website describe the same basic path:

`ESP32 reading -> MQTT event -> cloud worker -> saved record`

| Requirement | How the code follows it | Proof |
| --- | --- | --- |
| Upload a timestamped gateway reading | `p3_cloud.cpp` reads the BH1750 and creates a `room` event containing `recorded_at` and `lux` every 10 seconds. | The native formatter test and C++ to SQLite contract test pass. |
| Include station identity and session context where available | The `recording_start` formatter includes `node`, `attempt`, `book`, `passage` and the raw `fsr` value. | A real T03 start-packet parser result is formatted, accepted by the worker and read back from SQLite. |
| Use the website MQTT interface | Firmware publishes with TLS and QoS 1 to `lumpi/<gateway id>/events`. The message uses the website's `{"id":"device/boot/number","body":{...}}` envelope. | The test uses the actual compiled C++ topic and payload instead of a separately written Python example. |
| Reject insecure or malformed input | Firmware requires an `mqtts://` URI and CA certificate. The worker rejects the wrong topic, invalid JSON, oversized messages, mismatched gateway IDs and conflicting duplicate IDs. | Boundary and malformed-input tests pass; the worker has 100% statement and branch coverage. |
| Restore operation after reconnecting | ESP-MQTT automatic reconnect is enabled. The worker reconnects, subscribes again and continues storing new events. | A real local broker outage test saved one record before the outage and another after reconnection. |
| Make records retrievable | The worker stores each accepted event in SQLite by its unique event ID. Repeated delivery of the same ID and body is harmless. | Storage, retrieval, deduplication and conflicting-duplicate tests pass. |
| Keep credentials out of Git | ESP32 credentials stay in ignored `config/private.h`; worker credentials come from environment variables. | No private configuration, password file, key or certificate is tracked in Git. |
| Document any loss | T06 sends a fresh reading after reconnecting but does not save readings created while offline. A 30-second outage loses about three 10-second room samples. | This limitation is recorded here instead of claiming that offline buffering already exists. |

This matches the website's [message format](https://lumpi-build-guide.vercel.app/reference/messages), [gateway cloud behavior](https://lumpi-build-guide.vercel.app/gateway/sensors-cloud) and [cloud worker validation](https://lumpi-build-guide.vercel.app/gateway/worker).

The website's 64-file offline outbox and PUBACK retry system is not part of this checkpoint implementation. The Excel tracker assigns that stronger delivery behavior to T18. T06 only requires reconnect recovery with any loss documented.

The software evidence is complete, but the Excel completion sentence also requires actual hardware readings in the real private cloud. T06 should remain marked **In progress** until the ESP32 is uploaded, the Internet is disconnected and restored, and the before/after private-broker records are saved as evidence.

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
- Fixed the firmware-to-worker contract: firmware now publishes the required `id` and `body` envelope.
- Added a cross-language test that sends the actual C++ formatter output through the Python worker into SQLite.
- Changed the cloud worker to retry even when its first broker connection fails.
- Added explicit disconnect/retry logs and bounded reconnect delay.
- Added the guide-defined `recording_start` cloud event with node, session, book and raw FSR data.
- Passed both gateway room light and station FSR events through the C++ to SQLite contract test.
- Passed the actual T03 start-packet parser output into the station cloud record formatter.
- Passed a real local MQTT publish, broker outage, automatic reconnect and SQLite retrieval test.
- Passed an external TLS MQTT publish and SQLite retrieval test using both compiled C++ payloads and fake data.
- Checked every MQTT setup return value and report publish failure honestly.
- Expanded formatter tests for null buffers, truncation, invalid identifiers, infinities and range errors.

## Scope and known loss

- The Excel T06 checkpoint requires reconnect recovery and says to document any loss.
- This checkpoint creates a room record only while MQTT is connected. A 30-second outage therefore skips about three 10-second room samples; the first new sample is uploaded after reconnect.
- The website's final design prevents that loss with a 64-file LittleFS outbox and PUBACK retry. That is tracked separately as T18 (E4 ACK/retry and offline buffer), so it is not falsely claimed here.
- A real ESP32, BH1750, Wi-Fi network and private HiveMQ account are still required for the physical acceptance log.

## Requirement cross-check

| Source requirement | Code or evidence |
| --- | --- |
| Timestamped gateway raw reading | `room` event has `recorded_at` and `lux` |
| Timestamped station reading with context | `recording_start` has `recorded_at`, `node`, `attempt`, `book`, `passage` and `fsr` |
| MQTT topic | Actual C++ output is `lumpi/<gateway id>/events` |
| Cloud envelope | Actual C++ output has `id` and `body`; worker accepts and retrieves it |
| Reconnect restores upload | Real local MQTT outage/reconnect test stored records before and after reconnect |
| Document loss | Offline checkpoint samples are explicitly documented above |
| No credentials in Git | Real values stay in ignored `config/private.h` or environment variables |
| Website 64-file outbox | Deferred to its assigned T18/E4 task; not claimed as T06 work |

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
| Actual C++ payload accepted by worker | Passed locally |
| Timestamped station FSR and session context | Passed locally |
| Worker retries when broker starts offline | Passed locally |
| Real local MQTT outage and reconnect | Passed locally |
| External TLS MQTT publish and retrieval | Passed with fake data |
| Credentials absent from Git | Passed |
| C++ shared logic line coverage | 100% |
| Python worker statement/branch coverage | 100% |
| Real MQTT upload | Pending |
| Real reconnect | Pending |
