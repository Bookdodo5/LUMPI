// FSR402 pressure sensor, on the doll.
// 3.3V -> FSR -> GPIO35 -> 10k resistor -> GND.
// GPIO35 connects at the junction between the FSR and the resistor.
// Upload environment: s1b_pressure. Monitor: 115200.
// Press the FSR or put the book on it. The number should rise.
// Remove the book. The number should fall. Write down both ranges.
// This is an ADC reading (0..4095), not a weight measurement.
// Output: a table row every 500 ms (and at once when book ON/OFF changes):
// time | raw ADC | volts | book ON/OFF compared with doll_tuning::book_on_fsr.
// Guide: https://lumpi-build-guide.vercel.app/station/fsr
#include <Arduino.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

const int pressurePin = doll_pins::fsr;
const int threshold = doll_tuning::book_on_fsr;  // above this = a book is on the mat

int rows = 0;
bool lastBookOn = false;
unsigned long lastRowAt = 0;

void setup() {
    Serial.begin(115200);
    delay(500);
    analogReadResolution(12);
    analogSetPinAttenuation(pressurePin, ADC_11db);

    char pass[80];
    snprintf(pass, sizeof(pass), "near 0 when empty, above %d with a book on the mat.", threshold);
    monitor::title("STAGE 1b: PRESSURE MAT", "the mat's reading rises when a book is on it.", pass);
    monitor::section("WIRING");
    monitor::item("FSR leg 1", "3.3V");
    monitor::item("FSR leg 2", "GPIO%d (the signal)", pressurePin);
    monitor::item("10k resistor", "from GPIO%d to GND", pressurePin);
    monitor::item("Book threshold", "%d (doll_tuning::book_on_fsr in settings.h)", threshold);

    monitor::section("STARTUP CHECK (mat empty)");
    int first = analogRead(pressurePin);
    monitor::item("Empty reading", "%d of 4095", first);
    if (first > 3500) {
        monitor::fail("the empty mat reads almost full scale",
                      "the 10k resistor to GND is missing, or something is pressing the mat");
    }

    monitor::section("LIVE (put the book on, take it off)");
}

void loop() {
    int raw = analogRead(pressurePin);
    bool bookOn = raw > threshold;
    unsigned long now = millis();

    // Print on a change of verdict, or every 500 ms.
    if (bookOn != lastBookOn || now - lastRowAt >= 500) {
        if (rows % 20 == 0) {
            Serial.println("\n  time s |  raw ADC |  volts | verdict");
            Serial.println("  -------+----------+--------+----------------------------");
        }
        rows++;
        Serial.printf("  %6.1f |  %7d | %4.2f V | %s  (threshold %d)\n", now / 1000.0, raw,
                      raw * 3.3 / 4095, bookOn ? "book ON " : "book OFF", threshold);
        lastBookOn = bookOn;
        lastRowAt = now;
    }
    delay(50);
}
