// No parts connected. Just plug the ESP32 into USB.
// Upload: PlatformIO > Project Tasks > hello > Upload.
// Open Monitor at 115200. You should see "ESP32 is running" every second.
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
}

void loop() {
    Serial.println("ESP32 is running");
    delay(1000);
}
