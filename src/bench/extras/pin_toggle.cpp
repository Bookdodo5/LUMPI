// PIN TOGGLE: is an output pin alive? No audio, no I2S: plain HIGH / LOW.
// The speaker pins (5, 16, 17) and the alternative ones (2, 4, 15) switch together:
// 3 s HIGH (3.3 V), then 3 s LOW (0 V), forever. Measure each pin to GND with a multimeter
// on DC volts; disconnect anything wired to it first.
// Upload environment: x_pin_toggle. Monitor: 115200.
//   Pin follows HIGH/LOW            -> the pin works; a silent amp is a software or I2S question.
//   Pin stays at 0 V (or 3.3 V)     -> the pin is damaged, or shorted to GND (or 3.3 V).
#include <Arduino.h>
#include "monitor.h"

const int pins[] = {5, 16, 17, 2, 4, 15};

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("EXTRA: PIN TOGGLE", "output pins 5, 16, 17, 2, 4, 15 switch 3 s HIGH, 3 s LOW",
                   "each pin reads about 3.3 V when HIGH and about 0 V when LOW");
    for (int pin : pins) {
        pinMode(pin, OUTPUT);
    }
    Serial.println("\n  time s | all pins should read");
    Serial.println("  -------+---------------------");
}

void loop() {
    static bool high = false;
    high = !high;
    for (int pin : pins) {
        digitalWrite(pin, high ? HIGH : LOW);
    }
    Serial.printf("  %6.1f | %s\n", millis() / 1000.0f, high ? "HIGH = about 3.3 V" : "LOW  = about 0 V");
    delay(3000);
}
