// Gateway ACK button: GPIO13 -> button -> GND.
// Upload environment: ack_button. Monitor: 115200.
// One "ACK pressed" per press. Holding should not repeat.
// Guide: https://lumpi-build-guide.vercel.app/gateway/screen
#include <Arduino.h>

const int buttonPin = 13;
bool lastReading = HIGH;
bool stableReading = HIGH;
unsigned long changedAt = 0;

void setup() {
    Serial.begin(115200);
    pinMode(buttonPin, INPUT_PULLUP);
}

void loop() {
    bool reading = digitalRead(buttonPin);

    if (reading != lastReading) {
        changedAt = millis();
        lastReading = reading;
    }

    if (millis() - changedAt >= 35 && reading != stableReading) {
        stableReading = reading;
        if (reading == LOW) {
            Serial.println("ACK pressed");
        }
    }
    delay(2);
}
