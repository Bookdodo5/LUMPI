// I2C WIRE PROBE: the ESP32 acts as a multimeter for the SDA and SCL wires.
// Upload environment: i2c_probe. Monitor: 115200.
// Leave it running, unplug / re-plug ONE wire or module at a time, and watch the line change.
//
// How it tests a wire without a meter:
//   1. Turn on the chip's weak pull-UP. A wire that still reads LOW is tied to GND: a SHORT.
//   2. Turn on the chip's weak pull-DOWN. A wire that still reads HIGH has a module's
//      stronger pull-up resistor on it: the module is connected and powered.
#include <Arduino.h>
#include "settings.h"

const char* probe(int pin) {
    pinMode(pin, INPUT_PULLUP);
    delay(2);
    if (digitalRead(pin) == LOW) return "SHORT TO GND   <- fix this first";
    pinMode(pin, INPUT_PULLDOWN);
    delay(2);
    const bool module_found = digitalRead(pin) == HIGH;
    pinMode(pin, INPUT);
    return module_found ? "OK (a powered module is on it)" : "OPEN (no module reaches this pin)";
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nI2C WIRE PROBE. Unplug one thing at a time and watch which line changes.");
}

void loop() {
    Serial.printf("SDA (GPIO %d): %-36s | SCL (GPIO %d): %s\n", gateway_pins::sda,
                  probe(gateway_pins::sda), gateway_pins::scl, probe(gateway_pins::scl));
    delay(500);
}
