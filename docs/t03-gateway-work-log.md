# T03 Gateway work log

Owner: Tanakit  
Task: Gateway sensors and node packet reception

## Current status

Code compiles. Hardware has not been tested yet.

## Work completed

- Added `p2_i2c_scan` to find devices on the gateway I2C bus.
- Added labels for the BH1750 at `0x23` and OLED at `0x3C`.
- Added `p2_light` to read the BH1750 in lux.
- Added the shared gateway pin map in `config/settings.h`.
- Added the BH1750 functions in `src/drivers/real_io.cpp`.
- Added both environments to `platformio.ini`.
- Compiled `p2_i2c_scan` and `p2_light` successfully.

## Next work

1. Wire the BH1750 and OLED to GPIO 21 and GPIO 22.
2. Run `p2_i2c_scan` and confirm `0x23` and `0x3C`.
3. Run `p2_light` and record covered, room and flashlight values.
4. Test the HC-SR04 distance sensor.
5. Build and test node packet reception.

## Hardware results

| Test | Expected | Actual result | Result |
| --- | --- | --- | --- |
| I2C scan | `0x23` and `0x3C` | Not tested | Pending |
| Light sensor | Lux changes with light | Not tested | Pending |
| Distance sensor | Distance changes in cm | Not tested | Pending |
| Doll 1 packet | Identified as node 1 | Not built | Pending |
| Doll 2 packet | Identified as node 2 | Not built | Pending |
| Broken packet | Rejected safely | Not built | Pending |

## Problems and fixes

- None recorded yet.

## Evidence

- Wiring photo: pending
- I2C Serial Monitor output: pending
- Light measurements: pending
- Distance measurements: pending
- Packet logs: pending
