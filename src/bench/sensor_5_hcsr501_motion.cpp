// HC-SR501 PIR, on the doll.
// VCC -> ESP32 USB 5V, GND -> GND, OUT -> GPIO15.
// Upload environment: motion. Monitor: 115200.
// Wait 60 seconds after boot. Move in front of it, then stay still.
// Expect "Motion" when HIGH and "No motion" when LOW.
// The module holds HIGH for its delay setting; use its lowest delay to test.
// Guide: https://lumpi-build-guide.vercel.app/station/pir1
#include <Arduino.h>

const int motionPin = 33;

void setup() {
    Serial.begin(115200);
    pinMode(motionPin, INPUT);
    Serial.println("Wait 60 seconds for PIR warm-up...");
    delay(60000);
    Serial.println("Ready. Move in front of the sensor.");
}

void loop() {
    if (digitalRead(motionPin) == HIGH) {
        Serial.println("Motion");
    } else {
        Serial.println("No motion");
    }
    delay(500);
}
