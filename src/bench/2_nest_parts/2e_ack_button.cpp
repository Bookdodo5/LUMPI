// Gateway ACK button wiring: GPIO32 -> button -> GND.
// GPIO32 has an internal pull-up (34-39 do not). It does not share a pin with the OLED.
// Upload environment: s2e_ack_button. Monitor: 115200.
// Expect raw edges, one debounced press/release, and a status row every 5 seconds.
// HIGH = released, LOW = pressed. Holding must not increase the press count.
// Output: a table: time | raw pin | debounced state | presses | meaning.
// "raw edge" rows show contact bounce; only "PRESSED"/"released" rows count.
// Guide: https://lumpi-build-guide.vercel.app/gateway/screen
#include <Arduino.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

const int buttonPin = gateway_pins::ack;
bool lastReading = HIGH;
bool stableReading = HIGH;
unsigned long changedAt = 0;
unsigned long pressedAt = 0;
unsigned long lastStatusAt = 0;
unsigned long pressCount = 0;
int rows = 0;  // table rows printed; the header repeats every 20

void printRow(bool raw, const char* meaning) {
    if (rows % 20 == 0) {
        Serial.println("\n  time s | raw  | debounced | presses | meaning");
        Serial.println("  -------+------+-----------+---------+-----------------------------");
    }
    rows++;
    Serial.printf("  %6.1f | %-4s | %-9s | %7lu | %s\n", millis() / 1000.0, raw == HIGH ? "HIGH" : "LOW",
                  stableReading == HIGH ? "released" : "PRESSED", pressCount, meaning);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    pinMode(buttonPin, INPUT_PULLUP);
    monitor::title("STAGE 2e: ACK BUTTON",
                   "one press of the ACK button counts exactly once.",
                   "each press adds 1 to presses; holding it does not add more.");
    monitor::section("WIRING");
    monitor::item("ACK button", "GPIO%d -> button -> GND, no resistor", buttonPin);
    monitor::item("Pull-up", "inside the ESP32: HIGH = released, LOW = pressed");
    monitor::item("Debounce", "35 ms (a change must last 35 ms to count)");

    monitor::section("STARTUP CHECK (do not touch the button)");
    bool released = digitalRead(buttonPin) == HIGH;
    monitor::item("Raw pin", "%s", released ? "HIGH = released, OK" : "LOW = pressed?");
    if (!released) {
        monitor::fail("the button reads pressed while nobody touches it",
                      "check the wire is on the right pin and not touching GND");
    }
    monitor::section("LIVE (press, hold, release)");
}

void loop() {
    bool reading = digitalRead(buttonPin);
    unsigned long now = millis();

    if (reading != lastReading) {
        changedAt = now;
        lastReading = reading;
        printRow(reading, "raw edge (bounce is normal)");
    }

    if (now - changedAt >= 35 && reading != stableReading) {
        stableReading = reading;
        if (reading == LOW) {
            pressedAt = now;
            pressCount++;
            printRow(reading, "PRESS counted");
        } else {
            char meaning[40];
            snprintf(meaning, sizeof(meaning), "released after %lu ms", now - pressedAt);
            printRow(reading, meaning);
        }
    }

    if (now - lastStatusAt >= 5000) {
        lastStatusAt = now;
        char meaning[40] = "status: waiting for a press";
        if (stableReading == LOW) {
            snprintf(meaning, sizeof(meaning), "status: held for %lu ms", now - pressedAt);
        }
        printRow(reading, meaning);
    }
    delay(2);
}
