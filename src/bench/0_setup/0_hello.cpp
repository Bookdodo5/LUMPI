// No parts connected. Just plug the ESP32 into USB.
// Upload: PlatformIO > Project Tasks > s0_hello > Upload.
// Open Monitor at 115200. You should see a title, then one line per second:
// "<seconds> s  |  ESP32 is running". The seconds count up by 1 each line.
#include <Arduino.h>
#include "monitor.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 0: HELLO", "the board runs code and talks to this computer over USB.",
                   "one new line every second, seconds counting up 1, 2, 3 ...");
    monitor::section("WIRING");
    monitor::item("USB cable", "ESP32 to computer. Nothing else connected.");
    monitor::item("Monitor speed", "115200 baud (wrong speed = garbled letters)");
    monitor::section("LIVE (one line per second)");
}

void loop() {
    Serial.printf("  %5lu s  |  ESP32 is running\n", millis() / 1000);
    delay(1000);
}
