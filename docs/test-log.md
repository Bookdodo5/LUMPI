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
---

2026-11-19 / 2026-11-20

- Speaker (MAX98357A) works on the doll pins (BCLK 16, LRC 17, DIN 5). GAIN and SD left unconnected.
- puppet_2 speaker test now plays data/audio/song.wav (8 kHz mono 16-bit) from LittleFS on repeat. Run Upload Filesystem Image, then Upload.
- Decision 2026-11-20: PIR removed from the doll. E2 (interrupt-driven I/O) moves to the RECORD button, GPIO 13, FALLING-edge ISR. The ISR only notifies a task; the task debounces (35 ms) and acts. sensor_5 (PIR) test is obsolete.
- sensor_1 buttons test rewritten for the RECORD ISR. Not compiled or tested on hardware yet.
- Decision 2026-11-20: PREVIOUS/NEXT buttons replaced by a page knob (potentiometer as a dial): wiper GPIO 36 (VP), outer legs 3.3 V and GND. GPIO 39 and 15 free. settings.h: doll_pins::page_knob = 36.
- sensor_1 (env buttons) rewritten: RECORD ISR + STOP polled + knob dial. Compiles. doll_check, doll_check_sh1106, motion compile. Not tested on hardware.
- Decision 2026-11-20: RESUME = hold RECORD + STOP together for 2 s (both released and held again after the stop). Guide updated, not deployed.
