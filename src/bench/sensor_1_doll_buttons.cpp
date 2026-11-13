// Doll buttons. Each button goes between its GPIO and GND.
// PREVIOUS -> GPIO36, with a 10k resistor from GPIO36 to 3.3V.
// NEXT     -> GPIO39, with a 10k resistor from GPIO39 to 3.3V.
// RECORD   -> GPIO13. No external pull-up needed.
// STOP     -> GPIO14. No external pull-up needed.
// GPIO36/39 have no internal pull-up, so do not skip their resistors.
// Upload environment: buttons. Monitor: 115200.
// Press a button. Expect one line per press, even if you hold it down.
// Guide: https://lumpi-build-guide.vercel.app/station/buttons
#include <Arduino.h>

const int buttonPins[] = {36, 39, 13, 14};
const char* buttonNames[] = {"PREVIOUS", "NEXT", "RECORD", "STOP"};

bool lastReading[] = {HIGH, HIGH, HIGH, HIGH};
bool stableReading[] = {HIGH, HIGH, HIGH, HIGH};
unsigned long changedAt[] = {0, 0, 0, 0};

void setup() {
    Serial.begin(115200);
    pinMode(36, INPUT);  // External pull-up.
    pinMode(39, INPUT);  // External pull-up.
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
}

void loop() {
    for (int i = 0; i < 4; i++) {
        bool reading = digitalRead(buttonPins[i]);

        if (reading != lastReading[i]) {
            changedAt[i] = millis();
            lastReading[i] = reading;
        }

        // Ignore contact bounce. Accept a change only after 35 ms.
        if (millis() - changedAt[i] >= 35 && reading != stableReading[i]) {
            stableReading[i] = reading;

            if (reading == LOW) {
                Serial.print("Pressed: ");
                Serial.println(buttonNames[i]);
            }
        }
    }
    delay(2);
}
