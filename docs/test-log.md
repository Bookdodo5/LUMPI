# Test log

2026-11-12

- Added one test file per part. Wiring is at the top of each cpp.
- Renamed tests: pressure, light, servos, etc. No phase numbers.
- Removed the build script and long docs.
- Tests don't need Wi-Fi or the laptop AI.
- Hardware hasn't been tested yet.
- Rebuilt the 12 tests that use the installed toolchain/built-in libraries. All compiled.
- Downloaded RFID, OLED and BME280 libraries and their dependencies into .pio/libdeps.
- Built the remaining 4 tests successfully. All 16 tests now compile. No hardware test yet.
- Renamed adapter/ to laptop/ for the laptop server and AI code.

2026-11-13

- Numbered the 16 bench files in testing order; kept upload environment names.
- Put I2C scans before device tests, audio and servos later, optional BME280 last.
- Fixed stray text in the pressure build filter.

- Replaced global numbers with setup, sensor, gateway, puppet and optional groups, following the build guide tracks. Numbers now restart within each group.

---

setup_1 SUCCESS
gateway_1 SUCCESS
gateway_2 SUCCESS
gateway_3 SUCCESS
gateway_4 SUCCESS
gateway_5 SUCCESS
sensor_1 SUCCESS
sensor_2 SUCCESS
sensor_3 SUCCESS
sensor_4 NO
sensor_5 SUCCESS
sensor_6 SUCCESS
sensor_7 SUCCESS