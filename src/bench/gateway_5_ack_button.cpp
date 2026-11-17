// Gateway ACK button wiring: GPIO23 -> button -> GND.
// GPIO23 sits next to GPIO22, on the same side of the board as the other gateway pins.
// It does not share a pin with the OLED, so the OLED can stay connected.
// Upload environment: ack_button. Monitor: 115200.
// Expect raw edges, one debounced press/release, and status every second.
// HIGH = released, LOW = pressed. Holding must not increase the press count.
// Guide: https://lumpi-build-guide.vercel.app/gateway/screen
#include <Arduino.h>

const int buttonPin = 23;
bool lastReading = HIGH;
bool stableReading = HIGH;
unsigned long changedAt = 0;
unsigned long pressedAt = 0;
unsigned long lastStatusAt = 0;
unsigned long pressCount = 0;

void setup() {
    Serial.begin(115200);
    pinMode(buttonPin, INPUT_PULLUP);
    Serial.printf("ACK button test: GPIO%d -> button -> GND\n", buttonPin);
    Serial.println("INPUT_PULLUP: HIGH = released, LOW = pressed. Debounce: 35 ms.");
    Serial.printf("Initial raw state: %s\n", digitalRead(buttonPin) == HIGH ? "HIGH" : "LOW");
}

void loop() {
    bool reading = digitalRead(buttonPin);
    unsigned long now = millis();

    if (reading != lastReading) {
        changedAt = now;
        lastReading = reading;
        Serial.printf("[%lu ms] RAW GPIO%d: %s\n", now, buttonPin, reading == HIGH ? "HIGH" : "LOW");
    }

    if (now - changedAt >= 35 && reading != stableReading) {
        stableReading = reading;
        if (reading == LOW) {
            pressedAt = now;
            pressCount++;
            Serial.printf("ACK pressed #%lu\n", pressCount);
        } else {
            Serial.printf("ACK released after %lu ms\n", now - pressedAt);
        }
    }

    if (now - lastStatusAt >= 1000) {
        lastStatusAt = now;
        Serial.printf("STATUS GPIO%d: raw=%s, stable=%s, presses=%lu", buttonPin,
                      reading == HIGH ? "HIGH" : "LOW",
                      stableReading == HIGH ? "RELEASED" : "PRESSED", pressCount);
        if (stableReading == LOW) {
            Serial.printf(", held=%lu ms", now - pressedAt);
        }
        Serial.println();
    }
    delay(2);
}
