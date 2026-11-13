# LUMPI

Build guide: https://lumpi-build-guide.vercel.app

For now: testing the parts. Code is in `src/bench`.
Each cpp starts with the wiring and what you should see.

Open this folder in VS Code with PlatformIO installed.
In PlatformIO > Project Tasks, pick the test name, then click Upload.
Then click Monitor. Serial speed is 115200.

Start with `hello` and just the ESP32 plugged into USB.
Then follow the groups below, one part at a time. Numbers give the order
within each group. Run each I2C scan before its OLED or other I2C devices.
The sensor group uses doll wiring; the gateway group uses nest wiring.
The puppet group also uses doll wiring; test its STOP button first.
Opening a cpp does not select it for upload. Pick its environment in Project Tasks.
For the bottom Upload arrow, set `default_envs` in `platformio.ini` to your test name.

For servo commands, use the monitor with Enter-to-send:

```powershell
pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
```

Use ESP32-WROOM-32 boards. Disconnect power before changing wires.
Doll and gateway pin maps differ. Don't wire both roles onto the same board.
No Wi-Fi keys needed for these tests. Never commit `config/private.h` later.

Laptop server and AI code will go in `laptop/`.

Notes: [test log](docs/test-log.md).

Bench files are grouped by the build guide tracks. `sensor` means the doll
Sensor Node track, which includes its buttons and screen. Upload environment
names are unchanged; use the environment shown beside each filename.

[Board setup](https://lumpi-build-guide.vercel.app/foundation/hello):

- `setup_1_esp32_serial.cpp` → `hello`

[Sensor Node — doll](https://lumpi-build-guide.vercel.app/station/):

- `sensor_1_doll_buttons.cpp` → `buttons`
- `sensor_2_fsr402_pressure.cpp` → `pressure`
- `sensor_3_rc522_rfid.cpp` → `rfid`
- `sensor_4_inmp441_microphone.cpp` → `microphone`
- `sensor_5_hcsr501_motion.cpp` → `motion`
- `sensor_6_doll_i2c_scan.cpp` → `doll_i2c`
- `sensor_7_doll_ssd1306_oled.cpp` → `doll_oled`

[Gateway — nest](https://lumpi-build-guide.vercel.app/gateway/):

- `gateway_1_i2c_scan.cpp` → `gateway_i2c`
- `gateway_2_bh1750_light.cpp` → `light`
- `gateway_3_hcsr04_distance.cpp` → `distance`
- `gateway_4_ssd1306_oled.cpp` → `gateway_oled`
- `gateway_5_ack_button.cpp` → `ack_button`

[Puppet — doll outputs](https://lumpi-build-guide.vercel.app/station/puppet):

- `puppet_1_servos_power_stop.cpp` → `servos`
- `puppet_2_max98357a_speaker.cpp` → `speaker`

Optional spare (outside the main build):

- `optional_1_bme280_environment.cpp` → `bme280`
