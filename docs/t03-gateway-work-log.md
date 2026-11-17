# T03 Gateway work log

Owner: Tanakit  
Task: Gateway sensors and node packet reception

## Current status

Code compiles and local logic tests pass. Hardware has not been tested yet.

## Work completed

- Added `p2_i2c_scan` to find devices on the gateway I2C bus.
- Added labels for the BH1750 at `0x23` and OLED at `0x3C`.
- Added `p2_light` to read the BH1750 in lux.
- Added the shared gateway pin map in `config/settings.h`.
- Added the BH1750 functions in `src/drivers/real_io.cpp`.
- Added both environments to `platformio.ini`.
- Compiled `p2_i2c_scan` and `p2_light` successfully.
- Added a local test for I2C address labels, byte joining and lux conversion.
- Ran the local test successfully without an ESP32 or sensor.
- Added `p2_distance` with the guide's 10 microsecond trigger and 30 ms timeout.
- Added a local test for no-echo handling and distance conversion from 20–400 cm.
- Added the gateway start-packet parser for `node`, `attempt`, `passage`, `uid` and `fsr`.
- Tested node IDs 1, 2 and simulated node 3 without hardware.
- Tested missing values, non-numbers, bad node IDs, bad passages and number overflow.

## Next work

1. Wire the BH1750 and OLED to GPIO 21 and GPIO 22.
2. Run `p2_i2c_scan` and confirm `0x23` and `0x3C`.
3. Run `p2_light` and record covered, room and flashlight values.
4. Later, test the HC-SR04 on real hardware and calibrate the empty chair.
5. Later, send real HTTP packets from a doll when the Wi-Fi web server is built.

## Hardware results

| Test | Expected | Actual result | Result |
| --- | --- | --- | --- |
| I2C scan | `0x23` and `0x3C` | 2 devices found (addresses not recorded) | Partial |
| Light sensor | Lux changes with light | Error 263 timeouts, a few 0.0 lux reads; after reset "BH1750 not found" | Fail, wiring suspected |
| Distance sensor software | Local conversion tests pass | Passed locally | Pass |
| Distance sensor hardware | Distance changes in cm | Works (confirmed 2026-11-16) | Pass |
| Doll 1 packet logic | Identified as node 1 | Passed locally | Pass |
| Doll 2 packet logic | Identified as node 2 | Passed locally | Pass |
| Simulated node 3 logic | Identified as node 3 | Passed locally | Pass |
| Broken packet logic | Rejected safely | Passed locally | Pass |
| Real HTTP packet | Logged by gateway | Not tested | Pending |

## Problems and fixes

- BH1750 2026-11-16: intermittent I2C timeouts, then not found at boot. Same code reads when the link holds, so a loose or unsoldered connection is suspected. Recheck with gateway_i2c, OLED unplugged. USB also dropped a few times (POWERON_RESET).

## Evidence

- Wiring photo: pending
- I2C Serial Monitor output: pending
- Light measurements: pending
- Distance measurements: pending
- Packet logs: pending
