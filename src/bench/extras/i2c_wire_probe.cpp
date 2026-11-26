// I2C WIRE PROBE: the ESP32 acts as a multimeter for the SDA and SCL wires.
// Upload environment: x_i2c_probe. Monitor: 115200.
// Leave it running, unplug / re-plug ONE wire or module at a time, and watch the line change.
//
// How it tests a wire without a meter:
//   1. Turn on the chip's weak pull-UP. A wire that still reads LOW is tied to GND: a SHORT.
//   2. Turn on the chip's weak pull-DOWN. A wire that still reads HIGH has a module's
//      stronger pull-up resistor on it: the module is connected and powered.
// Output: a row when a result changes, and every 5 s anyway:
// time | SDA raw (pull-up read / pull-down read) + verdict | SCL the same.
#include <Arduino.h>
#include "settings.h"
#include "monitor.h"

int rows = 0;
String lastRow = "";
unsigned long lastRowAt = 0;

// Returns the verdict; writes the two raw reads ("H/L") into raw.
const char* probe(int pin, char* raw) {
    pinMode(pin, INPUT_PULLUP);
    delay(2);
    const bool up_read = digitalRead(pin) == HIGH;
    pinMode(pin, INPUT_PULLDOWN);
    delay(2);
    const bool down_read = digitalRead(pin) == HIGH;
    pinMode(pin, INPUT);
    sprintf(raw, "%c/%c", up_read ? 'H' : 'L', down_read ? 'H' : 'L');
    if (!up_read) return "SHORT TO GND <- fix first";
    return down_read ? "OK, powered module on it" : "OPEN, no module reaches";
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    monitor::title("EXTRA: I2C WIRE PROBE", "whether the SDA and SCL wires reach a powered module.",
                   "both wires say OK; unplugging one wire turns only its column to OPEN.");
    monitor::section("WIRING (nest I2C bus)");
    monitor::item("SDA", "GPIO%d", gateway_pins::sda);
    monitor::item("SCL", "GPIO%d", gateway_pins::scl);
    monitor::item("Modules", "VCC 3.3V and GND, as in the normal build");
    monitor::section("HOW TO READ raw");
    monitor::item("First letter", "read with the weak pull-UP:   L = shorted to GND");
    monitor::item("Second letter", "read with the weak pull-DOWN: H = a module's pull-up is there");
    monitor::section("LIVE (a row when something changes, or every 5 s)");
}

void loop() {
    char sdaRaw[4], sclRaw[4];
    const char* sda = probe(gateway_pins::sda, sdaRaw);
    const char* scl = probe(gateway_pins::scl, sclRaw);
    char row[120];
    snprintf(row, sizeof(row), "%s | %-25s | %s | %s", sdaRaw, sda, sclRaw, scl);

    if (lastRow != row || millis() - lastRowAt >= 5000) {
        if (rows % 20 == 0) {
            Serial.printf("\n  time s | SDA (GPIO%d)                     | SCL (GPIO%d)\n", gateway_pins::sda,
                          gateway_pins::scl);
            Serial.println("         | raw | verdict                   | raw | verdict");
            Serial.println("  -------+-----+---------------------------+-----+--------------------------");
        }
        rows++;
        Serial.printf("  %6.1f | %s\n", millis() / 1000.0, row);
        lastRow = row;
        lastRowAt = millis();
    }
    delay(500);
}
